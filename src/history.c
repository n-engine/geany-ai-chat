/*
 * history.c — Conversation history management for AI Chat plugin
 */

#include "history.h"
#include "prefs.h"
#include <string.h>

static gchar *history_json = NULL;

const gchar* history_get_json(void)
{
    return history_json;
}

gchar* json_escape(const gchar *s)
{
    GString *g = g_string_new("");
    for (const gchar *p = s; *p; ++p)
    {
        if (*p == '\\' || *p == '\"') g_string_append_c(g, '\\');
        if (*p == '\n') g_string_append(g, "\\n");
        else if (*p == '\r') g_string_append(g, "\\r");
        else g_string_append_c(g, *p);
    }
    return g_string_free(g, FALSE);
}

void json_append_double(GString *out, const char *key, double v)
{
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(buf, sizeof(buf), "%.6g", v);
    if (key && *key)
        g_string_append_printf(out, "\"%s\":%s", key, buf);
    else
        g_string_append(out, buf);
}

void history_add(const gchar *role, const gchar *content)
{
    gchar *esc = json_escape(content);
    gchar *msg = g_strdup_printf("{\"role\":\"%s\",\"content\":\"%s\"}", role, esc);
    g_free(esc);

    gchar *newhist = NULL;
    if (g_strcmp0(history_json, "[]") == 0)
        newhist = g_strdup_printf("[%s]", msg);
    else
        newhist = g_strdup_printf("%.*s,%s]", (int)strlen(history_json) - 1, history_json, msg);

    g_free(history_json);
    history_json = newhist;
    g_free(msg);
}

void history_init(void)
{
    g_free(history_json);
    history_json = g_strdup("[]");
    if (prefs.system_prompt && *prefs.system_prompt)
        history_add("system", prefs.system_prompt);
}

void history_free(void)
{
    g_clear_pointer(&history_json, g_free);
}
