/*
 * ai_chat.c — Geany plugin entry point (geany_load_module API)
 * Chat IA: streaming, editor selection, Stop,
 * GtkSourceView for code blocks
 *
 * Build:
 *   make
 */

#include <geanyplugin.h>
#include "prefs.h"
#include "history.h"
#include "network.h"
#include "ui.h"

static GeanyPlugin *g_plugin = NULL;

/* -------------------------- Lifecycle ------------------------------------ */

static gboolean my_plugin_init(GeanyPlugin *plugin, gpointer data)
{
    (void)data;
    g_plugin = plugin;
    prefs_load();
    if (!prefs.base_url) prefs_set_defaults();
    history_init();
    network_init();
    ui_build(plugin);
    return TRUE;
}

static void my_plugin_cleanup(GeanyPlugin *plugin, gpointer data)
{
    (void)plugin; (void)data;
    prefs_save();
    network_cleanup();
    history_free();
    prefs_free();
}

static PluginCallback callbacks[] = { { NULL, NULL, FALSE, NULL } };

G_MODULE_EXPORT
void geany_load_module(GeanyPlugin *plugin)
{
    plugin->info->name        = "AI Chat (pro)";
    plugin->info->description = "Chat local (Ollama/OpenAI) with streaming, "
                                "editor selection, Stop and colored code blocks.";
    plugin->info->version     = "2.0";
    plugin->info->author      = "Naskel";

    plugin->funcs->init      = my_plugin_init;
    plugin->funcs->cleanup   = my_plugin_cleanup;
    plugin->funcs->callbacks = callbacks;

    GEANY_PLUGIN_REGISTER(plugin, GEANY_API_VERSION);
}
