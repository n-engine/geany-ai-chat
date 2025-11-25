/*
 * network.h — HTTP/curl networking for AI Chat plugin
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <glib.h>
#include <gtk/gtk.h>
#include "prefs.h"

/* Request structure for async HTTP operations */
typedef struct Req
{
    gchar    *prompt;
    ApiMode   mode;
    gchar    *base;
    gchar    *model;
    gdouble   temp;
    gchar    *api_key;
    gboolean  streaming;

    volatile gint cancel;

    GString  *carry;    /* JSON-lines buffer (Ollama) */
    GString  *carry2;   /* SSE buffer (OpenAI) */

    GtkWidget     *row;
    GtkWidget     *stream_view;
    GtkTextBuffer *stream_buf;

    GString *accum;     /* Accumulated response text */
} Req;

/* Current request (for cancel support) */
extern Req *current_req;

/* Initialize curl globally */
void network_init(void);

/* Cleanup curl globally */
void network_cleanup(void);

/* Start async HTTP request in a new thread */
void network_send_request(Req *req);

/* Callbacks to be set by UI module */
typedef void (*StreamAppendFunc)(Req *req, const char *text, gssize len);
typedef void (*ReplaceRowFunc)(GtkWidget *row, const gchar *final_text);
typedef void (*SetBusyFunc)(gboolean busy);

void network_set_callbacks(StreamAppendFunc stream_append,
                           ReplaceRowFunc replace_row,
                           SetBusyFunc set_busy);

#endif /* NETWORK_H */
