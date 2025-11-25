/*
 * models.h — Model list fetching for AI Chat plugin
 */

#ifndef MODELS_H
#define MODELS_H

#include <glib.h>
#include "prefs.h"

/* Callback when models are fetched (called on main thread) */
typedef void (*ModelsFetchedCallback)(GList *models, gpointer user_data);

/*
 * Fetch available models asynchronously.
 * @param mode: API_OLLAMA or API_OPENAI
 * @param base_url: Base URL of the API
 * @param api_key: API key (for OpenAI, can be NULL for Ollama)
 * @param callback: Function to call when models are ready
 * @param user_data: User data passed to callback
 *
 * The callback receives a GList of gchar* model names.
 * The caller must free the list and strings with:
 *   g_list_free_full(models, g_free);
 */
void models_fetch_async(ApiMode mode,
                        const gchar *base_url,
                        const gchar *api_key,
                        ModelsFetchedCallback callback,
                        gpointer user_data);

#endif /* MODELS_H */
