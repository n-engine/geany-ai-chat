/*
 * ui.h — User interface for AI Chat plugin
 */

#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include <geanyplugin.h>
#include "network.h"

/* UI structure holding all widgets */
typedef struct
{
    GtkWidget    *root_box;

    GtkWidget    *msg_list;      /* GtkListBox for message bubbles */
    GtkWidget    *scroll;        /* Scrolled window for autoscroll */

    GtkWidget    *input_view;
    GtkTextBuffer*input_buf;

    GtkWidget    *btn_send;
    GtkWidget    *btn_send_sel;
    GtkWidget    *btn_stop;
    GtkWidget    *btn_clear;
    GtkWidget    *btn_reset;
    GtkWidget    *btn_copy_all;
    GtkWidget    *btn_export;

    GtkWidget    *cmb_api;
    GtkWidget    *ent_url;
    GtkWidget    *cmb_model;     /* GtkComboBoxText with entry for model selection */
    GtkWidget    *btn_refresh;   /* Refresh models list button */
    GtkWidget    *spin_temp;
    GtkWidget    *ent_key;
    GtkWidget    *chk_stream;
    GtkWidget    *chk_dark;
    GtkWidget    *chk_links;
    GtkWidget    *btn_emoji;
    GtkWidget    *btn_ctx;
    GtkWidget    *btn_network;
    GtkWidget    *btn_backends;

    gboolean      busy;
} Ui;

/* Global UI instance */
extern Ui ui;

/* Build the complete UI and attach to Geany */
void ui_build(GeanyPlugin *plugin);

/* Add a user message row */
void ui_add_user_row(const gchar *text);

/* Add an assistant streaming row (returns the row, sets up req) */
GtkWidget* ui_add_assistant_stream_row(Req *req);

/* Add an info row */
void ui_add_info_row(const gchar *text);

/* Autoscroll message list to bottom */
void ui_autoscroll_soon(void);

/* Copy text to clipboard */
void ui_copy_text_to_clipboard(const gchar *txt);

/* Send a prompt (creates request and starts network) */
void ui_send_prompt(const gchar *prompt);

#endif /* UI_H */
