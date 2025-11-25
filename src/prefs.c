/*
 * prefs.c — Preferences management for AI Chat plugin
 */

#include "prefs.h"
#include <glib.h>
#include <string.h>

AiPrefs prefs;
static gchar *conf_path = NULL;

/* --- Helper to free a PromptPreset --- */

static void preset_free(PromptPreset *p)
{
    if (!p) return;
    g_free(p->name);
    g_free(p->content);
    g_free(p);
}

static PromptPreset* preset_new(const gchar *name, const gchar *content)
{
    PromptPreset *p = g_new0(PromptPreset, 1);
    p->name = g_strdup(name);
    p->content = g_strdup(content);
    return p;
}

static PromptPreset* find_preset_by_name(const gchar *name)
{
    for (GList *l = prefs.prompt_presets; l; l = l->next)
    {
        PromptPreset *p = (PromptPreset *)l->data;
        if (g_strcmp0(p->name, name) == 0)
            return p;
    }
    return NULL;
}

/* --- Helper to free a BackendPreset --- */

static void backend_free(BackendPreset *b)
{
    if (!b) return;
    g_free(b->name);
    g_free(b->base_url);
    g_free(b->model);
    g_free(b->api_key);
    g_free(b);
}

static BackendPreset* backend_new(const gchar *name, ApiMode mode,
                                   const gchar *url, const gchar *model,
                                   gdouble temp, const gchar *key)
{
    BackendPreset *b = g_new0(BackendPreset, 1);
    b->name = g_strdup(name);
    b->api_mode = mode;
    b->base_url = g_strdup(url);
    b->model = g_strdup(model);
    b->temperature = temp;
    b->api_key = g_strdup(key);
    return b;
}

static BackendPreset* find_backend_by_name(const gchar *name)
{
    for (GList *l = prefs.backend_presets; l; l = l->next)
    {
        BackendPreset *b = (BackendPreset *)l->data;
        if (g_strcmp0(b->name, name) == 0)
            return b;
    }
    return NULL;
}

/* --- Defaults --- */

void prefs_set_defaults(void)
{
    prefs.api_mode    = API_OLLAMA;
    prefs.base_url    = g_strdup("http://127.0.0.1:11434");
    prefs.model       = g_strdup("llama3:8b");
    prefs.temperature = 0.2;
    prefs.api_key     = g_strdup("");
    prefs.streaming   = TRUE;
    prefs.dark_theme  = FALSE;
    prefs.system_prompt = g_strdup("");
    prefs.current_preset_name = NULL;
    prefs.prompt_presets = NULL;
    prefs.timeout     = 120;  /* 2 minutes default */
    prefs.proxy       = g_strdup("");
    prefs.current_backend_name = NULL;
    prefs.backend_presets = NULL;
    prefs.links_enabled = TRUE;  /* Links clickable by default */

    /* Add default presets */
    prefs_set_preset("Assistant général",
        "Tu es un assistant IA utile et concis. "
        "Réponds de manière claire et directe.");
    prefs_set_preset("Codeur expert",
        "Tu es un expert en programmation. "
        "Fournis du code propre, bien commenté et optimisé. "
        "Explique brièvement tes choix techniques.");
    prefs_set_preset("Relecteur",
        "Tu es un relecteur de code. "
        "Analyse le code fourni et suggère des améliorations "
        "concernant la lisibilité, la performance et la sécurité.");
}

void prefs_free(void)
{
    g_clear_pointer(&prefs.base_url, g_free);
    g_clear_pointer(&prefs.model,    g_free);
    g_clear_pointer(&prefs.api_key,  g_free);
    g_clear_pointer(&prefs.system_prompt, g_free);
    g_clear_pointer(&prefs.current_preset_name, g_free);
    g_clear_pointer(&prefs.proxy, g_free);
    g_clear_pointer(&prefs.current_backend_name, g_free);
    g_clear_pointer(&conf_path, g_free);

    /* Free presets lists */
    g_list_free_full(prefs.prompt_presets, (GDestroyNotify)preset_free);
    prefs.prompt_presets = NULL;
    g_list_free_full(prefs.backend_presets, (GDestroyNotify)backend_free);
    prefs.backend_presets = NULL;
}

/* --- Load --- */

void prefs_load(void)
{
    GKeyFile *kf = g_key_file_new();
    GError *err  = NULL;

    g_free(conf_path);
    conf_path = g_build_filename(g_get_user_config_dir(),
                                 "geany", "ai_chat.conf", NULL);

    if (!g_key_file_load_from_file(kf, conf_path, G_KEY_FILE_NONE, &err))
    {
        g_clear_error(&err);
        prefs_set_defaults();
        g_key_file_free(kf);
        return;
    }

    prefs.api_mode = (ApiMode) g_key_file_get_integer(kf, "chat", "api_mode", NULL);
    if (prefs.api_mode != API_OLLAMA && prefs.api_mode != API_OPENAI)
        prefs.api_mode = API_OLLAMA;

    g_free(prefs.base_url);
    prefs.base_url = g_key_file_get_string(kf, "chat", "base_url", NULL);
    if (!prefs.base_url) prefs.base_url = g_strdup("http://127.0.0.1:11434");

    g_free(prefs.model);
    prefs.model = g_key_file_get_string(kf, "chat", "model", NULL);
    if (!prefs.model) prefs.model = g_strdup("llama3:8b");

    prefs.temperature = g_key_file_get_double(kf, "chat", "temperature", NULL);
    if (prefs.temperature < 0.0 || prefs.temperature > 1.0) prefs.temperature = 0.2;

    g_free(prefs.api_key);
    prefs.api_key = g_key_file_get_string(kf, "chat", "api_key", NULL);
    if (!prefs.api_key) prefs.api_key = g_strdup("");

    prefs.streaming = g_key_file_get_boolean(kf, "chat", "streaming", NULL);

    if (g_key_file_has_key(kf, "chat", "dark_theme", NULL))
        prefs.dark_theme = g_key_file_get_boolean(kf, "chat", "dark_theme", NULL);
    else
        prefs.dark_theme = FALSE;

    g_free(prefs.system_prompt);
    prefs.system_prompt = g_key_file_get_string(kf, "chat", "system_prompt", NULL);
    if (!prefs.system_prompt) prefs.system_prompt = g_strdup("");

    g_free(prefs.current_preset_name);
    prefs.current_preset_name = g_key_file_get_string(kf, "chat", "current_preset", NULL);

    if (g_key_file_has_key(kf, "chat", "timeout", NULL))
        prefs.timeout = g_key_file_get_integer(kf, "chat", "timeout", NULL);
    else
        prefs.timeout = 120;
    if (prefs.timeout < 0) prefs.timeout = 0;

    g_free(prefs.proxy);
    prefs.proxy = g_key_file_get_string(kf, "chat", "proxy", NULL);
    if (!prefs.proxy) prefs.proxy = g_strdup("");

    if (g_key_file_has_key(kf, "chat", "links_enabled", NULL))
        prefs.links_enabled = g_key_file_get_boolean(kf, "chat", "links_enabled", NULL);
    else
        prefs.links_enabled = TRUE;

    /* Load presets */
    g_list_free_full(prefs.prompt_presets, (GDestroyNotify)preset_free);
    prefs.prompt_presets = NULL;

    if (g_key_file_has_group(kf, "presets"))
    {
        gint count = g_key_file_get_integer(kf, "presets", "count", NULL);
        for (gint i = 0; i < count; i++)
        {
            gchar *key_name = g_strdup_printf("preset_%d_name", i);
            gchar *key_content = g_strdup_printf("preset_%d_content", i);

            gchar *name = g_key_file_get_string(kf, "presets", key_name, NULL);
            gchar *content = g_key_file_get_string(kf, "presets", key_content, NULL);

            if (name && content)
            {
                PromptPreset *p = preset_new(name, content);
                prefs.prompt_presets = g_list_append(prefs.prompt_presets, p);
            }

            g_free(name);
            g_free(content);
            g_free(key_name);
            g_free(key_content);
        }
    }

    /* If no presets loaded, add defaults */
    if (!prefs.prompt_presets)
    {
        prefs_set_preset("Assistant général",
            "Tu es un assistant IA utile et concis. "
            "Réponds de manière claire et directe.");
        prefs_set_preset("Codeur expert",
            "Tu es un expert en programmation. "
            "Fournis du code propre, bien commenté et optimisé. "
            "Explique brièvement tes choix techniques.");
        prefs_set_preset("Relecteur",
            "Tu es un relecteur de code. "
            "Analyse le code fourni et suggère des améliorations "
            "concernant la lisibilité, la performance et la sécurité.");
    }

    /* Load backend presets */
    g_free(prefs.current_backend_name);
    prefs.current_backend_name = g_key_file_get_string(kf, "chat", "current_backend", NULL);

    g_list_free_full(prefs.backend_presets, (GDestroyNotify)backend_free);
    prefs.backend_presets = NULL;

    if (g_key_file_has_group(kf, "backends"))
    {
        gint count = g_key_file_get_integer(kf, "backends", "count", NULL);
        for (gint i = 0; i < count; i++)
        {
            gchar *key_name = g_strdup_printf("backend_%d_name", i);
            gchar *key_mode = g_strdup_printf("backend_%d_mode", i);
            gchar *key_url = g_strdup_printf("backend_%d_url", i);
            gchar *key_model = g_strdup_printf("backend_%d_model", i);
            gchar *key_temp = g_strdup_printf("backend_%d_temp", i);
            gchar *key_key = g_strdup_printf("backend_%d_key", i);

            gchar *name = g_key_file_get_string(kf, "backends", key_name, NULL);
            if (name)
            {
                ApiMode mode = (ApiMode) g_key_file_get_integer(kf, "backends", key_mode, NULL);
                gchar *url = g_key_file_get_string(kf, "backends", key_url, NULL);
                gchar *model = g_key_file_get_string(kf, "backends", key_model, NULL);
                gdouble temp = g_key_file_get_double(kf, "backends", key_temp, NULL);
                gchar *api_key = g_key_file_get_string(kf, "backends", key_key, NULL);

                BackendPreset *b = backend_new(name, mode,
                                               url ? url : "",
                                               model ? model : "",
                                               temp,
                                               api_key ? api_key : "");
                prefs.backend_presets = g_list_append(prefs.backend_presets, b);

                g_free(url);
                g_free(model);
                g_free(api_key);
            }

            g_free(name);
            g_free(key_name);
            g_free(key_mode);
            g_free(key_url);
            g_free(key_model);
            g_free(key_temp);
            g_free(key_key);
        }
    }

    g_key_file_free(kf);
}

/* --- Save --- */

void prefs_save(void)
{
    GKeyFile *kf = g_key_file_new();
    gchar *txt   = NULL;
    gsize  len   = 0;

    g_key_file_set_integer(kf, "chat", "api_mode", prefs.api_mode);
    g_key_file_set_string(kf,  "chat", "base_url", prefs.base_url);
    g_key_file_set_string(kf,  "chat", "model",    prefs.model);
    g_key_file_set_double(kf,  "chat", "temperature", prefs.temperature);
    g_key_file_set_string(kf,  "chat", "api_key",  prefs.api_key);
    g_key_file_set_boolean(kf, "chat", "streaming", prefs.streaming);
    g_key_file_set_boolean(kf, "chat", "dark_theme", prefs.dark_theme);
    g_key_file_set_string(kf,  "chat", "system_prompt", prefs.system_prompt ? prefs.system_prompt : "");
    if (prefs.current_preset_name)
        g_key_file_set_string(kf, "chat", "current_preset", prefs.current_preset_name);
    g_key_file_set_integer(kf, "chat", "timeout", prefs.timeout);
    g_key_file_set_string(kf,  "chat", "proxy", prefs.proxy ? prefs.proxy : "");
    g_key_file_set_boolean(kf, "chat", "links_enabled", prefs.links_enabled);

    /* Save presets */
    gint count = (gint)g_list_length(prefs.prompt_presets);
    g_key_file_set_integer(kf, "presets", "count", count);

    gint i = 0;
    for (GList *l = prefs.prompt_presets; l; l = l->next, i++)
    {
        PromptPreset *p = (PromptPreset *)l->data;
        gchar *key_name = g_strdup_printf("preset_%d_name", i);
        gchar *key_content = g_strdup_printf("preset_%d_content", i);

        g_key_file_set_string(kf, "presets", key_name, p->name);
        g_key_file_set_string(kf, "presets", key_content, p->content);

        g_free(key_name);
        g_free(key_content);
    }

    /* Save backend presets */
    if (prefs.current_backend_name)
        g_key_file_set_string(kf, "chat", "current_backend", prefs.current_backend_name);

    gint bcount = (gint)g_list_length(prefs.backend_presets);
    g_key_file_set_integer(kf, "backends", "count", bcount);

    gint bi = 0;
    for (GList *l = prefs.backend_presets; l; l = l->next, bi++)
    {
        BackendPreset *b = (BackendPreset *)l->data;
        gchar *key_name = g_strdup_printf("backend_%d_name", bi);
        gchar *key_mode = g_strdup_printf("backend_%d_mode", bi);
        gchar *key_url = g_strdup_printf("backend_%d_url", bi);
        gchar *key_model = g_strdup_printf("backend_%d_model", bi);
        gchar *key_temp = g_strdup_printf("backend_%d_temp", bi);
        gchar *key_key = g_strdup_printf("backend_%d_key", bi);

        g_key_file_set_string(kf, "backends", key_name, b->name);
        g_key_file_set_integer(kf, "backends", key_mode, b->api_mode);
        g_key_file_set_string(kf, "backends", key_url, b->base_url);
        g_key_file_set_string(kf, "backends", key_model, b->model);
        g_key_file_set_double(kf, "backends", key_temp, b->temperature);
        g_key_file_set_string(kf, "backends", key_key, b->api_key ? b->api_key : "");

        g_free(key_name);
        g_free(key_mode);
        g_free(key_url);
        g_free(key_model);
        g_free(key_temp);
        g_free(key_key);
    }

    txt = g_key_file_to_data(kf, &len, NULL);

    gchar *dir = g_path_get_dirname(conf_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    g_file_set_contents(conf_path, txt, (gssize)len, NULL);
    g_free(txt);
    g_key_file_free(kf);
}

/* --- Preset management --- */

GList* prefs_get_preset_names(void)
{
    GList *names = NULL;
    for (GList *l = prefs.prompt_presets; l; l = l->next)
    {
        PromptPreset *p = (PromptPreset *)l->data;
        names = g_list_append(names, p->name);
    }
    return names;
}

const gchar* prefs_get_preset_content(const gchar *name)
{
    PromptPreset *p = find_preset_by_name(name);
    return p ? p->content : NULL;
}

void prefs_set_preset(const gchar *name, const gchar *content)
{
    if (!name || !*name) return;

    PromptPreset *existing = find_preset_by_name(name);
    if (existing)
    {
        g_free(existing->content);
        existing->content = g_strdup(content);
    }
    else
    {
        PromptPreset *p = preset_new(name, content);
        prefs.prompt_presets = g_list_append(prefs.prompt_presets, p);
    }
}

void prefs_delete_preset(const gchar *name)
{
    for (GList *l = prefs.prompt_presets; l; l = l->next)
    {
        PromptPreset *p = (PromptPreset *)l->data;
        if (g_strcmp0(p->name, name) == 0)
        {
            prefs.prompt_presets = g_list_remove(prefs.prompt_presets, p);
            preset_free(p);

            /* Clear current preset if it was deleted */
            if (g_strcmp0(prefs.current_preset_name, name) == 0)
            {
                g_free(prefs.current_preset_name);
                prefs.current_preset_name = NULL;
            }
            return;
        }
    }
}

gboolean prefs_rename_preset(const gchar *old_name, const gchar *new_name)
{
    if (!old_name || !new_name || !*new_name) return FALSE;
    if (g_strcmp0(old_name, new_name) == 0) return TRUE;

    /* Check new name doesn't exist */
    if (find_preset_by_name(new_name)) return FALSE;

    PromptPreset *p = find_preset_by_name(old_name);
    if (!p) return FALSE;

    g_free(p->name);
    p->name = g_strdup(new_name);

    /* Update current preset name if needed */
    if (g_strcmp0(prefs.current_preset_name, old_name) == 0)
    {
        g_free(prefs.current_preset_name);
        prefs.current_preset_name = g_strdup(new_name);
    }

    return TRUE;
}

void prefs_apply_preset(const gchar *name)
{
    const gchar *content = prefs_get_preset_content(name);
    if (content)
    {
        g_free(prefs.system_prompt);
        prefs.system_prompt = g_strdup(content);
        g_free(prefs.current_preset_name);
        prefs.current_preset_name = g_strdup(name);
    }
}

/* --- Backend preset management --- */

GList* prefs_get_backend_names(void)
{
    GList *names = NULL;
    for (GList *l = prefs.backend_presets; l; l = l->next)
    {
        BackendPreset *b = (BackendPreset *)l->data;
        names = g_list_append(names, b->name);
    }
    return names;
}

const BackendPreset* prefs_get_backend(const gchar *name)
{
    return find_backend_by_name(name);
}

void prefs_save_backend(const gchar *name)
{
    if (!name || !*name) return;

    BackendPreset *existing = find_backend_by_name(name);
    if (existing)
    {
        /* Update existing */
        existing->api_mode = prefs.api_mode;
        g_free(existing->base_url);
        existing->base_url = g_strdup(prefs.base_url);
        g_free(existing->model);
        existing->model = g_strdup(prefs.model);
        existing->temperature = prefs.temperature;
        g_free(existing->api_key);
        existing->api_key = g_strdup(prefs.api_key);
    }
    else
    {
        /* Create new */
        BackendPreset *b = backend_new(name, prefs.api_mode,
                                       prefs.base_url, prefs.model,
                                       prefs.temperature, prefs.api_key);
        prefs.backend_presets = g_list_append(prefs.backend_presets, b);
    }

    g_free(prefs.current_backend_name);
    prefs.current_backend_name = g_strdup(name);
}

void prefs_delete_backend(const gchar *name)
{
    for (GList *l = prefs.backend_presets; l; l = l->next)
    {
        BackendPreset *b = (BackendPreset *)l->data;
        if (g_strcmp0(b->name, name) == 0)
        {
            prefs.backend_presets = g_list_remove(prefs.backend_presets, b);
            backend_free(b);

            /* Clear current backend if it was deleted */
            if (g_strcmp0(prefs.current_backend_name, name) == 0)
            {
                g_free(prefs.current_backend_name);
                prefs.current_backend_name = NULL;
            }
            return;
        }
    }
}

gboolean prefs_rename_backend(const gchar *old_name, const gchar *new_name)
{
    if (!old_name || !new_name || !*new_name) return FALSE;
    if (g_strcmp0(old_name, new_name) == 0) return TRUE;

    /* Check new name doesn't exist */
    if (find_backend_by_name(new_name)) return FALSE;

    BackendPreset *b = find_backend_by_name(old_name);
    if (!b) return FALSE;

    g_free(b->name);
    b->name = g_strdup(new_name);

    /* Update current backend name if needed */
    if (g_strcmp0(prefs.current_backend_name, old_name) == 0)
    {
        g_free(prefs.current_backend_name);
        prefs.current_backend_name = g_strdup(new_name);
    }

    return TRUE;
}

void prefs_apply_backend(const gchar *name)
{
    const BackendPreset *b = prefs_get_backend(name);
    if (b)
    {
        prefs.api_mode = b->api_mode;
        g_free(prefs.base_url);
        prefs.base_url = g_strdup(b->base_url);
        g_free(prefs.model);
        prefs.model = g_strdup(b->model);
        prefs.temperature = b->temperature;
        g_free(prefs.api_key);
        prefs.api_key = g_strdup(b->api_key);
        g_free(prefs.current_backend_name);
        prefs.current_backend_name = g_strdup(name);
    }
}
