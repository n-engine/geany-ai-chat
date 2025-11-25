/*
 * prefs.h — Preferences management for AI Chat plugin
 */

#ifndef PREFS_H
#define PREFS_H

#include <glib.h>

typedef enum { API_OLLAMA = 0, API_OPENAI = 1 } ApiMode;

/* System prompt preset */
typedef struct
{
    gchar *name;
    gchar *content;
} PromptPreset;

/* Backend configuration preset */
typedef struct
{
    gchar   *name;
    ApiMode  api_mode;
    gchar   *base_url;
    gchar   *model;
    gdouble  temperature;
    gchar   *api_key;
} BackendPreset;

typedef struct
{
    ApiMode  api_mode;
    gchar   *base_url;
    gchar   *model;
    gdouble  temperature;
    gchar   *api_key;
    gboolean streaming;
    gboolean dark_theme;
    gchar   *system_prompt;        /* Current active prompt content */
    gchar   *current_preset_name;  /* Name of current prompt preset (or NULL if custom) */
    GList   *prompt_presets;       /* List of PromptPreset* */
    gint     timeout;              /* Network timeout in seconds (0 = no limit) */
    gchar   *proxy;                /* Proxy URL (empty = no proxy) */
    gchar   *current_backend_name; /* Name of current backend preset (or NULL) */
    GList   *backend_presets;      /* List of BackendPreset* */
    gboolean links_enabled;        /* Enable clickable links in messages */
} AiPrefs;

/* Global preferences instance */
extern AiPrefs prefs;

/* Initialize preferences with default values */
void prefs_set_defaults(void);

/* Free all allocated preference strings */
void prefs_free(void);

/* Load preferences from config file */
void prefs_load(void);

/* Save preferences to config file */
void prefs_save(void);

/* --- Preset management --- */

/* Get list of preset names (caller must free with g_list_free, not strings) */
GList* prefs_get_preset_names(void);

/* Get preset content by name (returns NULL if not found) */
const gchar* prefs_get_preset_content(const gchar *name);

/* Add or update a preset */
void prefs_set_preset(const gchar *name, const gchar *content);

/* Delete a preset by name */
void prefs_delete_preset(const gchar *name);

/* Rename a preset */
gboolean prefs_rename_preset(const gchar *old_name, const gchar *new_name);

/* Apply a preset (sets system_prompt and current_preset_name) */
void prefs_apply_preset(const gchar *name);

/* --- Backend preset management --- */

/* Get list of backend preset names (caller must free with g_list_free, not strings) */
GList* prefs_get_backend_names(void);

/* Get backend preset by name (returns NULL if not found) */
const BackendPreset* prefs_get_backend(const gchar *name);

/* Add or update a backend preset from current settings */
void prefs_save_backend(const gchar *name);

/* Delete a backend preset by name */
void prefs_delete_backend(const gchar *name);

/* Rename a backend preset */
gboolean prefs_rename_backend(const gchar *old_name, const gchar *new_name);

/* Apply a backend preset (sets api_mode, base_url, model, temperature, api_key) */
void prefs_apply_backend(const gchar *name);

#endif /* PREFS_H */
