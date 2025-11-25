/*
 * history.h — Conversation history management for AI Chat plugin
 */

#ifndef HISTORY_H
#define HISTORY_H

#include <glib.h>

/* Get current history JSON string (read-only) */
const gchar* history_get_json(void);

/* Initialize/reset history (includes system prompt if set) */
void history_init(void);

/* Add a message to history */
void history_add(const gchar *role, const gchar *content);

/* Free history resources */
void history_free(void);

/* JSON escape utility (also used by network module) */
gchar* json_escape(const gchar *s);

/* Serialize double with ASCII dot (locale-independent) */
void json_append_double(GString *out, const char *key, double v);

#endif /* HISTORY_H */
