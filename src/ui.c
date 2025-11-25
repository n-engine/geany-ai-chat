/*
 * ui.c — User interface for AI Chat plugin
 */

#include "ui.h"
#include "prefs.h"
#include "history.h"
#include "network.h"
#include "ui_render.h"
#include "models.h"
#include <string.h>

Ui ui;
static GeanyPlugin *g_plugin = NULL;

/* --- Event blocker ------------------------------------------------------- */

static gboolean has_ancestor_of_type(GtkWidget *w, GType type)
{
    GtkWidget *p = w;
    while (p)
    {
        if (g_type_is_a(G_OBJECT_TYPE(p), type))
            return TRUE;
        p = gtk_widget_get_parent(p);
    }
    return FALSE;
}

static gboolean chat_event_blocker(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)widget; (void)user_data;

    if (!event)
        return FALSE;

    switch (event->type)
    {
        case GDK_BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
        case GDK_2BUTTON_PRESS:
        case GDK_MOTION_NOTIFY:
        case GDK_SCROLL:
            break;
        default:
            return FALSE;
    }

    GtkWidget *target = gtk_get_event_widget(event);
    if (!GTK_IS_WIDGET(target))
        return TRUE;

    if (has_ancestor_of_type(target, GTK_TYPE_BUTTON))
        return FALSE;

    if (GTK_IS_LABEL(target))
    {
        const gchar *cur = gtk_label_get_current_uri(GTK_LABEL(target));
        if (cur && *cur)
            return FALSE;
    }

    if (has_ancestor_of_type(target, GTK_SOURCE_TYPE_VIEW))
        return FALSE;

    return TRUE;
}

/* --- Autoscroll ---------------------------------------------------------- */

static gboolean autoscroll_idle_cb(gpointer data)
{
    (void)data;
    if (!ui.scroll) return FALSE;
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ui.scroll));
    if (!vadj) return FALSE;
    gdouble max = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
    if (max < 0) max = 0;
    gtk_adjustment_set_value(vadj, max);
    return FALSE;
}

void ui_autoscroll_soon(void) { g_idle_add(autoscroll_idle_cb, NULL); }

/* --- Clipboard ----------------------------------------------------------- */

void ui_copy_text_to_clipboard(const gchar *txt)
{
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, txt ? txt : "", -1);
}

/* --- Row helpers --------------------------------------------------------- */

static GtkWidget* make_row_container(void)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(row), outer);
    return row;
}

void ui_add_user_row(const gchar *text)
{
    GtkWidget *row = make_row_container();
    GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));

    GtkWidget *hdr = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>Vous</b>");
    gtk_label_set_markup(GTK_LABEL(hdr), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(hdr), 0.0);

    GtkWidget *lbl = gtk_label_new(NULL);
    gchar *markup_txt = mk_markup_with_links(text);
    gtk_label_set_markup(GTK_LABEL(lbl), markup_txt);
    g_free(markup_txt);
    gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
    gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
    g_signal_connect(lbl, "activate-link", G_CALLBACK(on_label_activate_link), NULL);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);

    gtk_box_pack_start(GTK_BOX(outer), hdr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), lbl, FALSE, FALSE, 0);

    gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
    gtk_widget_show_all(row);
    ui_autoscroll_soon();
}

GtkWidget* ui_add_assistant_stream_row(Req *req)
{
    GtkWidget *row = make_row_container();
    GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));

    GtkWidget *hdr = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>Assistant</b>");
    gtk_label_set_markup(GTK_LABEL(hdr), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(hdr), 0.0);

    GtkWidget *tv = gtk_text_view_new();
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_widget_set_name(tv, "stream-view");

    gtk_box_pack_start(GTK_BOX(outer), hdr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), tv,  FALSE, FALSE, 0);

    gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
    gtk_widget_show_all(row);
    ui_autoscroll_soon();

    req->row = row;
    req->stream_view = tv;
    req->stream_buf  = buf;
    return row;
}

void ui_add_info_row(const gchar *text)
{
    GtkWidget *row = make_row_container();
    GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));
    GtkWidget *lbl = gtk_label_new(text);
    gtk_box_pack_start(GTK_BOX(outer), lbl, FALSE, FALSE, 0);
    gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
    gtk_widget_show_all(row);
    ui_autoscroll_soon();
}

/* --- Network callbacks for UI -------------------------------------------- */

typedef struct {
    GtkTextBuffer *buf;
    gchar         *text;
} AppendCtx;

static gboolean append_idle_cb(gpointer data)
{
    AppendCtx *ctx = (AppendCtx*)data;
    if (ctx->buf && ctx->text)
    {
        GtkTextIter it;
        gtk_text_buffer_get_end_iter(ctx->buf, &it);
        gtk_text_buffer_insert(ctx->buf, &it, ctx->text, -1);
        ui_autoscroll_soon();
    }
    g_free(ctx->text);
    g_free(ctx);
    return FALSE;
}

static void ui_stream_append(Req *req, const char *text, gssize len)
{
    if (!text) return;
    AppendCtx *ctx = g_new0(AppendCtx, 1);
    ctx->buf  = req->stream_buf;
    ctx->text = len >= 0 ? g_strndup(text, (gsize)len) : g_strdup(text);
    g_idle_add(append_idle_cb, ctx);
}

typedef struct {
    GtkWidget *row;
    gchar     *final_text;
} ReplaceCtx;

static void replace_row_child(GtkWidget *row, GtkWidget *new_child)
{
    GtkWidget *old = gtk_bin_get_child(GTK_BIN(row));
    if (old) gtk_container_remove(GTK_CONTAINER(row), old);
    gtk_container_add(GTK_CONTAINER(row), new_child);
    gtk_widget_show_all(row);
    ui_autoscroll_soon();
}

static gboolean replace_row_idle_cb(gpointer data)
{
    ReplaceCtx *ctx = (ReplaceCtx*)data;
    if (ctx->row)
    {
        GtkWidget *comp = build_assistant_composite_from_markdown(ctx->final_text ? ctx->final_text : "");
        replace_row_child(ctx->row, comp);
    }
    g_free(ctx->final_text);
    g_free(ctx);
    return FALSE;
}

static void ui_replace_row(GtkWidget *row, const gchar *final_text)
{
    ReplaceCtx *ctx = g_new0(ReplaceCtx, 1);
    ctx->row = row;
    ctx->final_text = g_strdup(final_text);
    g_idle_add(replace_row_idle_cb, ctx);
}

static gboolean set_busy_idle_cb(gpointer data)
{
    gboolean on = GPOINTER_TO_INT(data);
    ui.busy = on;
    gtk_widget_set_sensitive(ui.btn_send,     !on);
    gtk_widget_set_sensitive(ui.btn_send_sel, !on);
    gtk_widget_set_sensitive(ui.btn_clear,    !on);
    gtk_widget_set_sensitive(ui.btn_reset,    !on);
    gtk_widget_set_sensitive(ui.btn_copy_all, !on);
    gtk_widget_set_sensitive(ui.btn_export,   !on);
    gtk_widget_set_sensitive(ui.btn_stop,      on);
    return FALSE;
}

static void ui_set_busy(gboolean busy)
{
    g_idle_add(set_busy_idle_cb, GINT_TO_POINTER(busy));
}

/* --- Preferences from UI ------------------------------------------------- */

static void read_prefs_from_ui(ApiMode *mode, gchar **base, gchar **model,
                               gdouble *temp, gchar **key, gboolean *stream)
{
    *mode  = (ApiMode) gtk_combo_box_get_active(GTK_COMBO_BOX(ui.cmb_api));
    *base  = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui.ent_url)));
    /* Get model from combo entry */
    GtkWidget *model_entry = gtk_bin_get_child(GTK_BIN(ui.cmb_model));
    *model = g_strdup(gtk_entry_get_text(GTK_ENTRY(model_entry)));
    *temp  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui.spin_temp));
    *key   = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui.ent_key)));
    *stream= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.chk_stream));
}

static void save_prefs_from_vals(ApiMode mode, const gchar *base, const gchar *model,
                                 gdouble temp, const gchar *key, gboolean stream)
{
    prefs.api_mode    = mode;
    g_free(prefs.base_url); prefs.base_url = g_strdup(base);
    g_free(prefs.model);    prefs.model    = g_strdup(model);
    prefs.temperature = temp;
    g_free(prefs.api_key);  prefs.api_key  = g_strdup(key);
    prefs.streaming   = stream;
    prefs_save();
}

/* --- Send prompt --------------------------------------------------------- */

void ui_send_prompt(const gchar *prompt)
{
    if (!prompt || !*prompt) return;

    ApiMode mode; gchar *base; gchar *model; gdouble temp;
    gchar *key; gboolean stream;
    read_prefs_from_ui(&mode, &base, &model, &temp, &key, &stream);
    save_prefs_from_vals(mode, base, model, temp, key, stream);

    ui_add_user_row(prompt);

    Req *req = g_new0(Req, 1);
    req->prompt    = g_strdup(prompt);
    req->mode      = mode;
    req->base      = base;
    req->model     = model;
    req->temp      = temp;
    req->api_key   = key;
    req->streaming = stream;
    req->accum     = g_string_new(NULL);
    g_atomic_int_set(&req->cancel, 0);

    ui_add_assistant_stream_row(req);
    network_send_request(req);
}

/* --- Button callbacks ---------------------------------------------------- */

static void on_send(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    if (ui.busy) return;
    GtkTextIter a, z;
    gtk_text_buffer_get_bounds(ui.input_buf, &a, &z);
    gchar *prompt = gtk_text_buffer_get_text(ui.input_buf, &a, &z, FALSE);
    ui_send_prompt(prompt);
    gtk_text_buffer_set_text(ui.input_buf, "", -1);
    g_free(prompt);
}

static void on_send_selection(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->editor || !doc->editor->sci)
    {
        ui_add_info_row("[Info] Aucun document actif.");
        return;
    }
    ScintillaObject *sci = doc->editor->sci;
    if (!sci_has_selection(sci))
    {
        ui_add_info_row("[Info] Aucune sélection.");
        return;
    }
    gchar *sel = sci_get_selection_contents(sci);
    if (!sel || !*sel)
    {
        g_free(sel);
        return;
    }
    GtkTextBuffer *ib = ui.input_buf;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ib, &end);
    if (gtk_text_buffer_get_char_count(ib) > 0)
        gtk_text_buffer_insert(ib, &end, "\n", -1);
    gtk_text_buffer_insert(ib, &end, sel, -1);
    g_free(sel);
    gtk_widget_grab_focus(ui.input_view);
}

static void on_clear(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GList *children = gtk_container_get_children(GTK_CONTAINER(ui.msg_list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
}

static void on_reset(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    history_init();
    ui_add_info_row("[Historique réinitialisé]");
}

static void on_stop(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    if (current_req)
    {
        g_atomic_int_set(&current_req->cancel, 1);
        GtkTextIter it;
        gtk_text_buffer_get_end_iter(current_req->stream_buf, &it);
        gtk_text_buffer_insert(current_req->stream_buf, &it, "\n[Stop demandé]\n", -1);
    }
}

static void on_copy_all(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GString *out = g_string_new("");
    GList *rows = gtk_container_get_children(GTK_CONTAINER(ui.msg_list));
    for (GList *r = rows; r; r = r->next)
    {
        GtkWidget *outer = gtk_bin_get_child(GTK_BIN(r->data));
        GList *kids = gtk_container_get_children(GTK_CONTAINER(outer));
        for (GList *k = kids; k; k = k->next)
        {
            if (GTK_IS_LABEL(k->data))
            {
                const gchar *t = gtk_label_get_text(GTK_LABEL(k->data));
                if (t) g_string_append_printf(out, "%s\n", t);
            }
            else if (GTK_IS_BOX(k->data))
            {
                GList *bb = gtk_container_get_children(GTK_CONTAINER(k->data));
                if (bb && bb->next && GTK_SOURCE_IS_VIEW(bb->next->data))
                {
                    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bb->next->data));
                    GtkTextIter a, z;
                    gtk_text_buffer_get_bounds(tb, &a, &z);
                    gchar *ct = gtk_text_buffer_get_text(tb, &a, &z, FALSE);
                    const gchar *lang_hint = "";
                    if (GTK_SOURCE_IS_BUFFER(tb))
                    {
                        GtkSourceLanguage *sl = gtk_source_buffer_get_language(GTK_SOURCE_BUFFER(tb));
                        if (sl)
                        {
                            const gchar *id = gtk_source_language_get_id(sl);
                            if (id && *id) lang_hint = id;
                            else
                            {
                                const gchar *nm = gtk_source_language_get_name(sl);
                                if (nm && *nm) lang_hint = nm;
                            }
                        }
                    }
                    if (lang_hint && *lang_hint)
                        g_string_append_printf(out, "```%s\n", lang_hint);
                    else
                        g_string_append(out, "```\n");
                    g_string_append(out, ct);
                    g_string_append(out, "\n```\n");
                    g_free(ct);
                }
                if (bb) g_list_free(bb);
            }
        }
        if (kids) g_list_free(kids);
    }
    if (rows) g_list_free(rows);
    ui_copy_text_to_clipboard(out->str);
    g_string_free(out, TRUE);
}

static gchar* generate_conversation_markdown(void)
{
    GString *out = g_string_new("# Conversation AI Chat\n\n");
    GList *rows = gtk_container_get_children(GTK_CONTAINER(ui.msg_list));

    for (GList *r = rows; r; r = r->next)
    {
        GtkWidget *outer = gtk_bin_get_child(GTK_BIN(r->data));
        GList *kids = gtk_container_get_children(GTK_CONTAINER(outer));
        gboolean is_first = TRUE;

        for (GList *k = kids; k; k = k->next)
        {
            if (GTK_IS_LABEL(k->data))
            {
                const gchar *t = gtk_label_get_text(GTK_LABEL(k->data));
                if (t && *t)
                {
                    /* Check if it's a header (Vous/Assistant) */
                    if (is_first && (g_str_has_prefix(t, "Vous") || g_str_has_prefix(t, "Assistant")))
                    {
                        g_string_append_printf(out, "## %s\n\n", t);
                    }
                    else if (is_first && g_str_has_prefix(t, "["))
                    {
                        /* Info message */
                        g_string_append_printf(out, "*%s*\n\n", t);
                    }
                    else
                    {
                        g_string_append_printf(out, "%s\n\n", t);
                    }
                    is_first = FALSE;
                }
            }
            else if (GTK_IS_BOX(k->data))
            {
                GList *bb = gtk_container_get_children(GTK_CONTAINER(k->data));
                if (bb && bb->next && GTK_SOURCE_IS_VIEW(bb->next->data))
                {
                    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bb->next->data));
                    GtkTextIter a, z;
                    gtk_text_buffer_get_bounds(tb, &a, &z);
                    gchar *ct = gtk_text_buffer_get_text(tb, &a, &z, FALSE);
                    const gchar *lang_hint = "";

                    if (GTK_SOURCE_IS_BUFFER(tb))
                    {
                        GtkSourceLanguage *sl = gtk_source_buffer_get_language(GTK_SOURCE_BUFFER(tb));
                        if (sl)
                        {
                            const gchar *id = gtk_source_language_get_id(sl);
                            if (id && *id) lang_hint = id;
                        }
                    }

                    if (lang_hint && *lang_hint)
                        g_string_append_printf(out, "```%s\n", lang_hint);
                    else
                        g_string_append(out, "```\n");
                    g_string_append(out, ct);
                    if (ct[strlen(ct)-1] != '\n')
                        g_string_append(out, "\n");
                    g_string_append(out, "```\n\n");
                    g_free(ct);
                }
                if (bb) g_list_free(bb);
            }
        }
        if (kids) g_list_free(kids);
        g_string_append(out, "---\n\n");
    }
    if (rows) g_list_free(rows);

    return g_string_free(out, FALSE);
}

static void on_export(GtkButton *b, gpointer u)
{
    (void)b; (void)u;

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Exporter la conversation",
        GTK_WINDOW(gtk_widget_get_toplevel(ui.root_box)),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "Annuler", GTK_RESPONSE_CANCEL,
        "Enregistrer", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "conversation.md");

    /* Add filters */
    GtkFileFilter *md_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(md_filter, "Markdown (*.md)");
    gtk_file_filter_add_pattern(md_filter, "*.md");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), md_filter);

    GtkFileFilter *txt_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(txt_filter, "Texte (*.txt)");
    gtk_file_filter_add_pattern(txt_filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), txt_filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "Tous les fichiers");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gchar *content = generate_conversation_markdown();
        GError *err = NULL;

        if (g_file_set_contents(filename, content, -1, &err))
        {
            gchar *msg = g_strdup_printf("[Conversation exportée: %s]", filename);
            ui_add_info_row(msg);
            g_free(msg);
        }
        else
        {
            gchar *msg = g_strdup_printf("[Erreur export: %s]", err->message);
            ui_add_info_row(msg);
            g_free(msg);
            g_error_free(err);
        }

        g_free(content);
        g_free(filename);
    }

    gtk_widget_destroy(dlg);
}

static void on_toggle_dark(GtkToggleButton *tb, gpointer user_data)
{
    (void)user_data;
    prefs.dark_theme = gtk_toggle_button_get_active(tb);
    prefs_save();
    apply_theme_css();
    update_code_schemes_in_widget(ui.msg_list);
}

static void on_toggle_links(GtkToggleButton *tb, gpointer user_data)
{
    (void)user_data;
    prefs.links_enabled = gtk_toggle_button_get_active(tb);
    prefs_save();
}

/* --- Context dialog with presets ----------------------------------------- */

typedef struct {
    GtkWidget *combo;
    GtkWidget *textview;
    GtkTextBuffer *buffer;
    gchar *editing_preset;  /* Name of preset being edited, or NULL for custom */
} ContextDialogData;

static void populate_preset_combo(GtkComboBoxText *combo, const gchar *select_name)
{
    gtk_combo_box_text_remove_all(combo);
    gtk_combo_box_text_append(combo, "_custom_", "(Personnalisé)");

    GList *names = prefs_get_preset_names();
    gint idx = 0;
    gint select_idx = 0;

    for (GList *l = names; l; l = l->next, idx++)
    {
        const gchar *name = (const gchar *)l->data;
        gtk_combo_box_text_append(combo, name, name);
        if (g_strcmp0(name, select_name) == 0)
            select_idx = idx + 1;  /* +1 because of custom entry */
    }
    g_list_free(names);

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), select_idx);
}

static void on_preset_combo_changed(GtkComboBox *combo, gpointer user_data)
{
    ContextDialogData *data = (ContextDialogData *)user_data;
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));

    g_free(data->editing_preset);

    if (g_strcmp0(id, "_custom_") == 0)
    {
        data->editing_preset = NULL;
        gtk_text_buffer_set_text(data->buffer, prefs.system_prompt ? prefs.system_prompt : "", -1);
    }
    else
    {
        data->editing_preset = g_strdup(id);
        const gchar *content = prefs_get_preset_content(id);
        gtk_text_buffer_set_text(data->buffer, content ? content : "", -1);
    }
}

static void on_preset_new(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ContextDialogData *data = (ContextDialogData *)user_data;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Nouveau preset",
                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Créer", GTK_RESPONSE_OK,
                        NULL);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nom du preset...");
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name)
        {
            /* Get current text as content for new preset */
            GtkTextIter a, z;
            gtk_text_buffer_get_bounds(data->buffer, &a, &z);
            gchar *content = gtk_text_buffer_get_text(data->buffer, &a, &z, FALSE);

            prefs_set_preset(name, content);
            g_free(content);

            populate_preset_combo(GTK_COMBO_BOX_TEXT(data->combo), name);
            g_free(data->editing_preset);
            data->editing_preset = g_strdup(name);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_preset_delete(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ContextDialogData *data = (ContextDialogData *)user_data;

    if (!data->editing_preset) return;  /* Can't delete custom */

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Supprimer le preset \"%s\" ?", data->editing_preset);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES)
    {
        prefs_delete_preset(data->editing_preset);
        g_free(data->editing_preset);
        data->editing_preset = NULL;
        populate_preset_combo(GTK_COMBO_BOX_TEXT(data->combo), NULL);
        gtk_text_buffer_set_text(data->buffer, "", -1);
    }
    gtk_widget_destroy(dlg);
}

static void on_preset_rename(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ContextDialogData *data = (ContextDialogData *)user_data;

    if (!data->editing_preset) return;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Renommer le preset",
                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Renommer", GTK_RESPONSE_OK,
                        NULL);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), data->editing_preset);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        const gchar *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name && *new_name && g_strcmp0(new_name, data->editing_preset) != 0)
        {
            if (prefs_rename_preset(data->editing_preset, new_name))
            {
                g_free(data->editing_preset);
                data->editing_preset = g_strdup(new_name);
                populate_preset_combo(GTK_COMBO_BOX_TEXT(data->combo), new_name);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_context_clicked(GtkButton *b, gpointer u)
{
    (void)b; (void)u;

    ContextDialogData data = {0};

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Contexte système",
                        GTK_WINDOW(gtk_widget_get_toplevel(ui.root_box)),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Appliquer", GTK_RESPONSE_OK,
                        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 500, 400);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 8);

    /* Preset selection row */
    GtkWidget *preset_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *preset_label = gtk_label_new("Preset:");
    data.combo = gtk_combo_box_text_new();
    GtkWidget *btn_new = gtk_button_new_with_label("+");
    GtkWidget *btn_del = gtk_button_new_with_label("-");
    GtkWidget *btn_rename = gtk_button_new_with_label("✎");
    gtk_widget_set_tooltip_text(btn_new, "Nouveau preset");
    gtk_widget_set_tooltip_text(btn_del, "Supprimer le preset");
    gtk_widget_set_tooltip_text(btn_rename, "Renommer le preset");

    gtk_box_pack_start(GTK_BOX(preset_row), preset_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(preset_row), data.combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(preset_row), btn_new, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(preset_row), btn_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(preset_row), btn_rename, FALSE, FALSE, 0);

    /* Text editor */
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    data.textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data.textview), GTK_WRAP_WORD_CHAR);
    data.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data.textview));
    gtk_container_add(GTK_CONTAINER(sw), data.textview);

    /* Info label */
    GtkWidget *info = gtk_label_new("Le prompt système définit le comportement de l'assistant.");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_widget_set_margin_top(info, 8);

    gtk_box_pack_start(GTK_BOX(area), preset_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), sw, TRUE, TRUE, 8);
    gtk_box_pack_start(GTK_BOX(area), info, FALSE, FALSE, 0);

    /* Initialize */
    data.editing_preset = g_strdup(prefs.current_preset_name);
    populate_preset_combo(GTK_COMBO_BOX_TEXT(data.combo), prefs.current_preset_name);
    gtk_text_buffer_set_text(data.buffer, prefs.system_prompt ? prefs.system_prompt : "", -1);

    /* Connect signals */
    g_signal_connect(data.combo, "changed", G_CALLBACK(on_preset_combo_changed), &data);
    g_signal_connect(btn_new, "clicked", G_CALLBACK(on_preset_new), &data);
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_preset_delete), &data);
    g_signal_connect(btn_rename, "clicked", G_CALLBACK(on_preset_rename), &data);

    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        /* Save current text */
        GtkTextIter a, z;
        gtk_text_buffer_get_bounds(data.buffer, &a, &z);
        gchar *txt = gtk_text_buffer_get_text(data.buffer, &a, &z, FALSE);

        /* Update preset content if editing one */
        if (data.editing_preset)
        {
            prefs_set_preset(data.editing_preset, txt);
            prefs_apply_preset(data.editing_preset);
        }
        else
        {
            /* Custom prompt */
            g_free(prefs.system_prompt);
            prefs.system_prompt = g_strdup(txt);
            g_free(prefs.current_preset_name);
            prefs.current_preset_name = NULL;
        }

        g_free(txt);
        prefs_save();
        history_init();
        ui_add_info_row("[Contexte système mis à jour]");
    }

    g_free(data.editing_preset);
    gtk_widget_destroy(dlg);
}

/* --- Network settings dialog --------------------------------------------- */

static void on_network_clicked(GtkButton *b, gpointer u)
{
    (void)b; (void)u;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Paramètres réseau",
                        GTK_WINDOW(gtk_widget_get_toplevel(ui.root_box)),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Appliquer", GTK_RESPONSE_OK,
                        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 400, -1);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    /* Timeout */
    GtkWidget *lbl_timeout = gtk_label_new("Timeout (secondes) :");
    gtk_widget_set_halign(lbl_timeout, GTK_ALIGN_END);
    GtkWidget *spin_timeout = gtk_spin_button_new_with_range(0, 600, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_timeout), prefs.timeout);
    gtk_widget_set_tooltip_text(spin_timeout, "0 = pas de limite");

    gtk_grid_attach(GTK_GRID(grid), lbl_timeout, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin_timeout, 1, 0, 1, 1);

    /* Proxy */
    GtkWidget *lbl_proxy = gtk_label_new("Proxy HTTP :");
    gtk_widget_set_halign(lbl_proxy, GTK_ALIGN_END);
    GtkWidget *ent_proxy = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent_proxy), prefs.proxy ? prefs.proxy : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_proxy), "http://proxy:port (vide = direct)");
    gtk_widget_set_hexpand(ent_proxy, TRUE);

    gtk_grid_attach(GTK_GRID(grid), lbl_proxy, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_proxy, 1, 1, 1, 1);

    /* Info */
    GtkWidget *info = gtk_label_new("Le proxy supporte HTTP/HTTPS/SOCKS5.");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_widget_set_margin_top(info, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(info), "dim-label");

    gtk_box_pack_start(GTK_BOX(area), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), info, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        prefs.timeout = (gint) gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_timeout));
        g_free(prefs.proxy);
        prefs.proxy = g_strdup(gtk_entry_get_text(GTK_ENTRY(ent_proxy)));
        prefs_save();
        ui_add_info_row("[Paramètres réseau mis à jour]");
    }

    gtk_widget_destroy(dlg);
}

/* --- Backends dialog ----------------------------------------------------- */

typedef struct {
    GtkWidget *combo;
    gchar *editing_backend;
} BackendsDialogData;

static void populate_backend_combo(GtkComboBoxText *combo, const gchar *select_name)
{
    gtk_combo_box_text_remove_all(combo);

    GList *names = prefs_get_backend_names();
    gint idx = 0;
    gint select_idx = -1;

    for (GList *l = names; l; l = l->next, idx++)
    {
        const gchar *name = (const gchar *)l->data;
        gtk_combo_box_text_append(combo, name, name);
        if (g_strcmp0(name, select_name) == 0)
            select_idx = idx;
    }
    g_list_free(names);

    if (select_idx >= 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), select_idx);
    else if (idx > 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

static void sync_ui_to_prefs(void)
{
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.cmb_api), prefs.api_mode == API_OPENAI ? 1 : 0);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_url), prefs.base_url);
    GtkWidget *model_entry = gtk_bin_get_child(GTK_BIN(ui.cmb_model));
    gtk_entry_set_text(GTK_ENTRY(model_entry), prefs.model);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.spin_temp), prefs.temperature);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_key), prefs.api_key);
}

static void on_backend_combo_changed(GtkComboBox *combo, gpointer user_data)
{
    BackendsDialogData *data = (BackendsDialogData *)user_data;
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));

    g_free(data->editing_backend);
    data->editing_backend = g_strdup(id);
}

static void on_backend_save(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    BackendsDialogData *data = (BackendsDialogData *)user_data;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Sauvegarder la config",
                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Sauvegarder", GTK_RESPONSE_OK,
                        NULL);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *entry = gtk_entry_new();
    if (data->editing_backend)
        gtk_entry_set_text(GTK_ENTRY(entry), data->editing_backend);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nom du preset (ex: Ollama local)");
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name)
        {
            prefs_save_backend(name);
            prefs_save();
            populate_backend_combo(GTK_COMBO_BOX_TEXT(data->combo), name);
            g_free(data->editing_backend);
            data->editing_backend = g_strdup(name);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_backend_load(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    BackendsDialogData *data = (BackendsDialogData *)user_data;

    if (!data->editing_backend) return;

    prefs_apply_backend(data->editing_backend);
    prefs_save();
    sync_ui_to_prefs();
    history_init();
    ui_add_info_row("[Backend chargé, historique réinitialisé]");
}

static void on_backend_delete(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    BackendsDialogData *data = (BackendsDialogData *)user_data;

    if (!data->editing_backend) return;

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Supprimer le backend \"%s\" ?", data->editing_backend);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES)
    {
        prefs_delete_backend(data->editing_backend);
        prefs_save();
        g_free(data->editing_backend);
        data->editing_backend = NULL;
        populate_backend_combo(GTK_COMBO_BOX_TEXT(data->combo), NULL);
    }
    gtk_widget_destroy(dlg);
}

static void on_backend_rename(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    BackendsDialogData *data = (BackendsDialogData *)user_data;

    if (!data->editing_backend) return;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Renommer le backend",
                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->combo))),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Annuler", GTK_RESPONSE_CANCEL,
                        "Renommer", GTK_RESPONSE_OK,
                        NULL);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), data->editing_backend);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
    {
        const gchar *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name && *new_name && g_strcmp0(new_name, data->editing_backend) != 0)
        {
            if (prefs_rename_backend(data->editing_backend, new_name))
            {
                prefs_save();
                g_free(data->editing_backend);
                data->editing_backend = g_strdup(new_name);
                populate_backend_combo(GTK_COMBO_BOX_TEXT(data->combo), new_name);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_backends_clicked(GtkButton *b, gpointer u)
{
    (void)b; (void)u;

    BackendsDialogData data = {0};

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Presets de backends",
                        GTK_WINDOW(gtk_widget_get_toplevel(ui.root_box)),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        "Fermer", GTK_RESPONSE_CLOSE,
                        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 450, -1);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);

    /* Info */
    GtkWidget *info = gtk_label_new(
        "Sauvegardez et chargez des configurations complètes\n"
        "(API, URL, modèle, température, clé).");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_widget_set_margin_bottom(info, 12);

    /* Combo + buttons row */
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    data.combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(data.combo, TRUE);
    GtkWidget *btn_load = gtk_button_new_with_label("Charger");
    GtkWidget *btn_save = gtk_button_new_with_label("Sauver");
    GtkWidget *btn_del = gtk_button_new_with_label("-");
    GtkWidget *btn_rename = gtk_button_new_with_label("✎");
    gtk_widget_set_tooltip_text(btn_load, "Charger ce preset");
    gtk_widget_set_tooltip_text(btn_save, "Sauvegarder la config actuelle");
    gtk_widget_set_tooltip_text(btn_del, "Supprimer");
    gtk_widget_set_tooltip_text(btn_rename, "Renommer");

    gtk_box_pack_start(GTK_BOX(row), data.combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn_load, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn_save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn_rename, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(area), info, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), row, FALSE, FALSE, 0);

    /* Initialize */
    data.editing_backend = g_strdup(prefs.current_backend_name);
    populate_backend_combo(GTK_COMBO_BOX_TEXT(data.combo), prefs.current_backend_name);

    /* Connect signals */
    g_signal_connect(data.combo, "changed", G_CALLBACK(on_backend_combo_changed), &data);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_backend_load), &data);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_backend_save), &data);
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_backend_delete), &data);
    g_signal_connect(btn_rename, "clicked", G_CALLBACK(on_backend_rename), &data);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));

    g_free(data.editing_backend);
    gtk_widget_destroy(dlg);
}

/* --- Emoji --------------------------------------------------------------- */

static void insert_emoji_to_input(const gchar *emoji)
{
    GtkTextIter it;
    GtkTextMark *mark = gtk_text_buffer_get_insert(ui.input_buf);
    gtk_text_buffer_get_iter_at_mark(ui.input_buf, &it, mark);
    gtk_text_buffer_insert(ui.input_buf, &it, emoji, -1);
}

static void on_emoji_click(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GtkWidget *menu = gtk_menu_new();
    const gchar *emojis[] = {
        "🙂","😂","😅","😉","🤔","😎","😍","👍","👎","🚀",
        "🔥","🧠","🐛","✅","❌","⏳","📌", NULL
    };
    for (int i = 0; emojis[i]; ++i)
    {
        GtkWidget *mi = gtk_menu_item_new_with_label(emojis[i]);
        g_signal_connect_swapped(mi, "activate",
                                 G_CALLBACK(insert_emoji_to_input),
                                 (gpointer)emojis[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
#if GTK_CHECK_VERSION(3,22,0)
    gtk_menu_popup_at_widget(GTK_MENU(menu), ui.btn_emoji,
                             GDK_GRAVITY_SOUTH_EAST,
                             GDK_GRAVITY_NORTH_EAST, NULL);
#else
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
#endif
}

/* --- Input key handler --------------------------------------------------- */

static gboolean on_input_key(GtkWidget *w, GdkEventKey *e, gpointer u)
{
    (void)w; (void)u;

    /* Enter (without Shift) = send */
    if (e->keyval == GDK_KEY_Return && !(e->state & GDK_SHIFT_MASK))
    {
        if (!ui.busy)
            g_signal_emit_by_name(ui.btn_send, "clicked");
        return TRUE;
    }

    /* Escape = stop (when busy) */
    if (e->keyval == GDK_KEY_Escape && ui.busy)
    {
        g_signal_emit_by_name(ui.btn_stop, "clicked");
        return TRUE;
    }

    /* Ctrl+Shift+C = copy all conversation */
    if (e->keyval == GDK_KEY_C &&
        (e->state & GDK_CONTROL_MASK) &&
        (e->state & GDK_SHIFT_MASK))
    {
        g_signal_emit_by_name(ui.btn_copy_all, "clicked");
        return TRUE;
    }

    return FALSE;
}

/* --- UI helpers ---------------------------------------------------------- */

static GtkWidget *make_labeled_entry(const gchar *label, GtkWidget **entry_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *lab = gtk_label_new(label);
    gtk_widget_set_halign(lab, GTK_ALIGN_START);
    GtkWidget *ent = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(box), lab, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ent, TRUE, TRUE, 0);
    if (entry_out) *entry_out = ent;
    return box;
}

/* --- Models list refresh ------------------------------------------------- */

static void on_models_fetched(GList *models, gpointer user_data)
{
    (void)user_data;

    if (!ui.cmb_model) return;

    /* Save current text */
    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(ui.cmb_model));
    const gchar *current = gtk_entry_get_text(GTK_ENTRY(entry));
    gchar *saved = g_strdup(current);

    /* Clear existing items */
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui.cmb_model));

    /* Add fetched models */
    for (GList *l = models; l != NULL; l = l->next)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cmb_model),
                                       (const gchar *)l->data);
    }

    /* Restore current text (or set first model if empty) */
    if (saved && *saved)
    {
        gtk_entry_set_text(GTK_ENTRY(entry), saved);
    }
    else if (models)
    {
        gtk_entry_set_text(GTK_ENTRY(entry), (const gchar *)models->data);
    }

    g_free(saved);
    g_list_free_full(models, g_free);
}

static void refresh_models_list(void)
{
    ApiMode mode = (ApiMode) gtk_combo_box_get_active(GTK_COMBO_BOX(ui.cmb_api));
    const gchar *base = gtk_entry_get_text(GTK_ENTRY(ui.ent_url));
    const gchar *key = gtk_entry_get_text(GTK_ENTRY(ui.ent_key));

    models_fetch_async(mode, base, key, on_models_fetched, NULL);
}

static void on_refresh_clicked(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    refresh_models_list();
}

static void on_api_changed(GtkComboBox *combo, gpointer u)
{
    (void)combo; (void)u;
    /* Reset history and refresh models list when API changes */
    history_init();
    ui_add_info_row("[Historique réinitialisé]");
    refresh_models_list();
}

/* --- Build UI ------------------------------------------------------------ */

void ui_build(GeanyPlugin *plugin)
{
    g_plugin = plugin;

    /* Register network callbacks */
    network_set_callbacks(ui_stream_append, ui_replace_row, ui_set_busy);

    GtkWidget *nb = plugin->geany_data->main_widgets->message_window_notebook;

    ui.root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.root_box), "ai-chat");
    gtk_widget_add_events(ui.root_box,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
    g_signal_connect(ui.root_box, "event",
                     G_CALLBACK(chat_event_blocker), NULL);
    apply_theme_css();

    GtkWidget *opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui.cmb_api = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cmb_api), "Ollama");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cmb_api), "OpenAI-compat");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.cmb_api), prefs.api_mode == API_OPENAI ? 1 : 0);

    GtkWidget *url_box = make_labeled_entry("URL", &ui.ent_url);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_url), prefs.base_url);

    /* Model combo with entry and refresh button */
    GtkWidget *model_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *model_lab = gtk_label_new("Modèle");
    gtk_widget_set_halign(model_lab, GTK_ALIGN_START);
    ui.cmb_model = gtk_combo_box_text_new_with_entry();
    GtkWidget *model_entry = gtk_bin_get_child(GTK_BIN(ui.cmb_model));
    gtk_entry_set_text(GTK_ENTRY(model_entry), prefs.model);
    gtk_entry_set_placeholder_text(GTK_ENTRY(model_entry), "Sélectionner ou saisir...");
    ui.btn_refresh = gtk_button_new_with_label("↻");
    gtk_widget_set_tooltip_text(ui.btn_refresh, "Rafraîchir la liste des modèles");
    g_signal_connect(ui.btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(model_box), model_lab, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(model_box), ui.cmb_model, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(model_box), ui.btn_refresh, FALSE, FALSE, 0);

    ui.spin_temp = gtk_spin_button_new_with_range(0.0, 1.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.spin_temp), prefs.temperature);

    GtkWidget *temp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *lab_t = gtk_label_new("Temp");
    gtk_box_pack_start(GTK_BOX(temp_box), lab_t, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(temp_box), ui.spin_temp, FALSE, FALSE, 0);

    ui.chk_stream = gtk_check_button_new_with_label("Streaming");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.chk_stream), prefs.streaming);
    ui.chk_dark = gtk_check_button_new_with_label("Sombre");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.chk_dark), prefs.dark_theme);
    g_signal_connect(ui.chk_dark, "toggled", G_CALLBACK(on_toggle_dark), NULL);
    ui.chk_links = gtk_check_button_new_with_label("Liens");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.chk_links), prefs.links_enabled);
    g_signal_connect(ui.chk_links, "toggled", G_CALLBACK(on_toggle_links), NULL);
    ui.btn_ctx = gtk_button_new_with_label("Contexte…");
    g_signal_connect(ui.btn_ctx, "clicked", G_CALLBACK(on_context_clicked), NULL);
    ui.btn_network = gtk_button_new_with_label("Réseau…");
    g_signal_connect(ui.btn_network, "clicked", G_CALLBACK(on_network_clicked), NULL);
    ui.btn_backends = gtk_button_new_with_label("Backends…");
    g_signal_connect(ui.btn_backends, "clicked", G_CALLBACK(on_backends_clicked), NULL);

    GtkWidget *key_box = make_labeled_entry("Clé", &ui.ent_key);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_key), prefs.api_key);

    gtk_box_pack_start(GTK_BOX(opts), ui.cmb_api, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), url_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(opts), model_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(opts), temp_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.chk_stream, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.chk_dark, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.chk_links, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.btn_ctx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.btn_network, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.btn_backends, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), key_box, TRUE, TRUE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    ui.scroll = scroll;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    ui.msg_list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), ui.msg_list);

    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui.btn_emoji = gtk_button_new_with_label("🙂");
    g_signal_connect(ui.btn_emoji, "clicked", G_CALLBACK(on_emoji_click), NULL);

    GtkWidget *input_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(input_scroll), "input-wrap");
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(input_scroll), 64);
    ui.input_view = gtk_text_view_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.input_view), "input");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui.input_view), GTK_WRAP_WORD_CHAR);
    ui.input_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.input_view));
    g_signal_connect(ui.input_view, "key-press-event", G_CALLBACK(on_input_key), NULL);
    gtk_container_add(GTK_CONTAINER(input_scroll), ui.input_view);

    gtk_box_pack_start(GTK_BOX(input_row), ui.btn_emoji, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_row), input_scroll, TRUE, TRUE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui.btn_send      = gtk_button_new_with_label("Envoyer (Entrée)");
    ui.btn_send_sel  = gtk_button_new_with_label("Envoyer sélection");
    ui.btn_stop      = gtk_button_new_with_label("Stop");
    ui.btn_clear     = gtk_button_new_with_label("Effacer");
    ui.btn_reset     = gtk_button_new_with_label("Réinit. histo");
    ui.btn_copy_all  = gtk_button_new_with_label("Copier tout");
    ui.btn_export    = gtk_button_new_with_label("Exporter…");

    g_signal_connect(ui.btn_send,     "clicked", G_CALLBACK(on_send), NULL);
    g_signal_connect(ui.btn_send_sel, "clicked", G_CALLBACK(on_send_selection), NULL);
    g_signal_connect(ui.btn_stop,     "clicked", G_CALLBACK(on_stop), NULL);
    g_signal_connect(ui.btn_clear,    "clicked", G_CALLBACK(on_clear), NULL);
    g_signal_connect(ui.btn_reset,    "clicked", G_CALLBACK(on_reset), NULL);
    g_signal_connect(ui.btn_copy_all, "clicked", G_CALLBACK(on_copy_all), NULL);
    g_signal_connect(ui.btn_export,   "clicked", G_CALLBACK(on_export), NULL);

    g_signal_connect(ui.cmb_api, "changed", G_CALLBACK(on_api_changed), NULL);
    g_signal_connect(ui.cmb_model, "changed", G_CALLBACK(on_reset), NULL);

    gtk_box_pack_start(GTK_BOX(btns), ui.btn_send,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_send_sel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_stop,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_clear,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_reset,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_copy_all, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_export,   FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(ui.root_box), opts,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), scroll, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), input_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), btns,   FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), ui.root_box, gtk_label_new("Chat IA"));
    gtk_widget_show_all(ui.root_box);

    gtk_widget_set_sensitive(ui.btn_stop, FALSE);

    /* Load models list on startup */
    refresh_models_list();
}
