/*
 * ai_chat.c — Geany plugin (geany_load_module API)
 * Chat IA: streaming, sélection éditeur, Stop,
 * GtkSourceView pour code blocks, sans trampolines GCC (no execstack)
 *
 * Build (GtkSourceView 3.x) :
 *   gcc -fPIC -shared -O2 -Wall -Wextra \
 *     $(pkg-config --cflags geany gtk+-3.0 gtksourceview-3.0) \
 *     -o ai_chat.so ai_chat.c \
 *     $(pkg-config --libs geany gtk+-3.0 gtksourceview-3.0) \
 *     -lcurl -lgthread-2.0 -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now
 */

#include <geanyplugin.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <curl/curl.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include <geany/document.h>

static GeanyPlugin *g_plugin = NULL;

/* ---------------------------- Prefs ------------------------------------- */

typedef enum { API_OLLAMA = 0, API_OPENAI = 1 } ApiMode;

typedef struct
{
    ApiMode  api_mode;
    gchar   *base_url;
    gchar   *model;
    gdouble  temperature;
    gchar   *api_key;
    gboolean streaming;
    gboolean dark_theme;
} AiPrefs;

static AiPrefs prefs;
static gchar  *conf_path = NULL;

static void prefs_set_defaults(void)
{
    prefs.api_mode    = API_OLLAMA;
    prefs.base_url    = g_strdup("http://127.0.0.1:11434");
    prefs.model       = g_strdup("llama3:8b");
    prefs.temperature = 0.2;
    prefs.api_key     = g_strdup("");
    prefs.streaming   = TRUE;
    prefs.dark_theme  = FALSE;
}

static void prefs_free(void)
{
    g_clear_pointer(&prefs.base_url, g_free);
    g_clear_pointer(&prefs.model,    g_free);
    g_clear_pointer(&prefs.api_key,  g_free);
}

static void prefs_load(void)
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

    g_key_file_free(kf);
}

static void prefs_save(void)
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

    txt = g_key_file_to_data(kf, &len, NULL);
    g_mkdir_with_parents(g_path_get_dirname(conf_path), 0700);
    g_file_set_contents(conf_path, txt, (gssize)len, NULL);
    g_free(txt);
    g_key_file_free(kf);
}

/* ------------------------------ UI ------------------------------------- */

typedef struct
{
    GtkWidget    *root_box;

    GtkWidget    *msg_list;      /* GtkListBox des bulles de messages */
    GtkWidget    *scroll;        /* Scrolled window pour autoscroll */

    GtkWidget    *input_view;
    GtkTextBuffer*input_buf;

    GtkWidget    *btn_send;
    GtkWidget    *btn_send_sel;
    GtkWidget    *btn_stop;
    GtkWidget    *btn_clear;
    GtkWidget    *btn_reset;
    GtkWidget    *btn_copy_all;

    GtkWidget    *cmb_api;
    GtkWidget    *ent_url;
    GtkWidget    *ent_model;
    GtkWidget    *spin_temp;
    GtkWidget    *ent_key;
    GtkWidget    *chk_stream;
    GtkWidget    *chk_dark;
    GtkWidget    *btn_emoji;

    gboolean      busy;
} Ui;

static Ui ui;


/* ---------- Clickable links (labels with Pango <a href=...>) ---------- */
/* Convert plaintext + Markdown [label](url) + bare URLs into Pango markup with <a>. */
/* No linkification inside triple-backtick code fences. */

static gboolean
on_label_activate_link(GtkLabel *label, const gchar *uri, gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;

    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(label));
    if (!GTK_IS_WINDOW(toplevel))
        toplevel = NULL;

    gtk_show_uri_on_window(GTK_WINDOW(toplevel), uri, GDK_CURRENT_TIME, &err);
    if (err)
    {
        g_warning("AI Chat: open link failed: %s", err->message);
        g_clear_error(&err);
    }
    return TRUE; /* handled — do not propagate to Geany handlers */
}

static inline gboolean
is_url_char(gunichar c)
{
    if (g_unichar_isalnum(c)) return TRUE;
    switch (c)
    {
        case '/': case ':': case '?': case '#': case '&': case '=':
        case '%': case '.': case '-': case '_': case '+': case '~':
        case '@': case '!': case '*': case '\'': case '(': case ')':
            return TRUE;
        default: return FALSE;
    }
}

static void
append_escaped(GString *out, const gchar *s, gssize len)
{
    if (len < 0) len = (gssize)strlen(s);
    gchar *esc = g_markup_escape_text(s, (gssize)len);
    g_string_append(out, esc);
    g_free(esc);
}

static gchar*
mk_markup_with_links(const gchar *src)
{
    if (!src) return g_strdup("");
    const gchar *p = src;
    GString *out = g_string_new(NULL);
    GString *plain = g_string_new(NULL);
    gboolean in_fence = FALSE;

    while (*p)
    {
        if (!in_fence && p[0]=='`' && p[1]=='`' && p[2]=='`')
        {
            if (plain->len) { append_escaped(out, plain->str, plain->len); g_string_set_size(plain, 0); }
            in_fence = TRUE;
            const gchar *q = strstr(p+3, "```");
            if (!q) { append_escaped(out, p, -1); break; }
            append_escaped(out, p, (q+3)-p);
            p = q+3;
            in_fence = FALSE;
            continue;
        }

        if (!in_fence && *p == '[')
        {
            const gchar *lb = p + 1;
            const gchar *rb = NULL;
            for (const gchar *t = lb; *t; ++t)
            {
                if (*t == '\\' && t[1]) { ++t; continue; }
                if (*t == ']') { rb = t; break; }
                if (*t == '\n') break;
            }
            if (rb && rb[1] == '(')
            {
                const gchar *ub = NULL;
                const gchar *u = rb + 2;
                if (u[0] != 0)
                {
                    for (const gchar *t = u; *t; ++t)
                    {
                        if (*t == '\\' && t[1]) { ++t; continue; }
                        if (*t == ')') { ub = t; break; }
                        if (*t == '\n') break;
                    }
                }
                if (ub && ub > u)
                {
                    if (plain->len) { append_escaped(out, plain->str, plain->len); g_string_set_size(plain, 0); }

                    gchar *label = g_strndup(lb, rb - lb);
                    gchar *url   = g_strndup(u,  ub - u);

                    if (g_str_has_prefix(url, "www."))
                    {
                        gchar *tmp = g_strconcat("https://", url, NULL);
                        g_free(url);
                        url = tmp;
                    }

                    gchar *url_esc = g_markup_escape_text(url, -1);
                    g_string_append_printf(out, "<a href=\"%s\">", url_esc);
                    g_free(url_esc);

                    append_escaped(out, label, -1);
                    g_string_append(out, "</a>");

                    g_free(label);
                    g_free(url);

                    p = ub + 1;
                    continue;
                }
            }
        }

        if (!in_fence && (g_str_has_prefix(p, "http://") || g_str_has_prefix(p, "https://") || g_str_has_prefix(p, "www.")))
        {
            const gchar *q = p;
            while (*q)
            {
                gunichar ch = g_utf8_get_char(q);
                if (!is_url_char(ch)) break;
                q = g_utf8_next_char(q);
            }
            while (q > p && strchr(").,;:!?", (unsigned char)q[-1]) != NULL)
                q--;

            if (plain->len) { append_escaped(out, plain->str, plain->len); g_string_set_size(plain, 0); }

            gchar *disp = g_strndup(p, q - p);
            gchar *href;
            if (g_str_has_prefix(disp, "www."))
                href = g_strconcat("https://", disp, NULL);
            else
                href = g_strdup(disp);

            gchar *href_esc = g_markup_escape_text(href, -1);
            g_string_append_printf(out, "<a href=\"%s\">", href_esc);
            g_free(href_esc);

            append_escaped(out, disp, -1);
            g_string_append(out, "</a>");

            g_free(disp);
            g_free(href);

            p = q;
            continue;
        }

        plain = g_string_append_c(plain, *p);
        p++;
    }

    if (plain->len) { append_escaped(out, plain->str, plain->len); }

    g_string_free(plain, TRUE);
    return g_string_free(out, FALSE);
}



/* ---------- Clickable links support ---------- */

typedef struct
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *url;
} LinkTagCtx;

static gboolean
open_link_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;
    if (event->type != GDK_BUTTON_RELEASE || event->button != 1)
        return FALSE;

    GtkTextIter iter;
    gint x, y;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          (gint) event->x,
                                          (gint) event->y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);

    GSList *tags = gtk_text_iter_get_tags(&iter);
    for (GSList *l = tags; l != NULL; l = l->next)
    {
        GtkTextTag *tag = l->data;
        const gchar *url = g_object_get_data(G_OBJECT(tag), "url");
        if (url)
        {
            GError *err = NULL;
            GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
            if (!GTK_IS_WINDOW(toplevel))
                toplevel = NULL;
            gtk_show_uri_on_window(GTK_WINDOW(toplevel),
                                   url,
                                   GDK_CURRENT_TIME,
                                   &err);
            if (err)
            {
                g_warning("Failed to open link: %s", err->message);
                g_clear_error(&err);
            }
            g_slist_free(tags);
            return TRUE; /* stop event propagation */
        }
    }
    g_slist_free(tags);
    return FALSE;
}

static gboolean
motion_notify_cb(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    (void)user_data;
    GtkTextIter iter;
    gint x, y;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          (gint) event->x,
                                          (gint) event->y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);

    gboolean over_link = FALSE;
    GSList *tags = gtk_text_iter_get_tags(&iter);
    for (GSList *l = tags; l != NULL; l = l->next)
    {
        GtkTextTag *tag = l->data;
        if (g_object_get_data(G_OBJECT(tag), "url"))
        {
            over_link = TRUE;
            break;
        }
    }
    g_slist_free(tags);

    GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                              GTK_TEXT_WINDOW_TEXT);
    if (win)
    {
        GdkCursor *cursor = gdk_cursor_new_from_name(
            gdk_window_get_display(win),
            over_link ? "pointer" : "text");
        gdk_window_set_cursor(win, cursor);
        g_clear_object(&cursor);
    }

    return FALSE;
}

static void
apply_link_tag(GtkTextBuffer *buffer,
               GtkTextIter *start,
               GtkTextIter *end,
               const gchar *url)
{
    static guint link_counter = 0;
    gchar *tagname = g_strdup_printf("link-%u", link_counter++);
    GtkTextTag *tag = gtk_text_buffer_create_tag(
        buffer, tagname,
        "underline", PANGO_UNDERLINE_SINGLE,
        "foreground", "blue",
        NULL);
    g_object_set_data_full(G_OBJECT(tag), "url", g_strdup(url), g_free);
    gtk_text_buffer_apply_tag(buffer, tag, start, end);
    g_free(tagname);
}

/* very basic regex-like search for URLs */
static void
linkify_text(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end)
{
    gchar *text = gtk_text_buffer_get_text(buffer, start, end, FALSE);
    const gchar *p = text;
    while ((p = g_strstr_len(p, -1, "http")) != NULL)
    {
        const gchar *q = p;
        while (*q && !g_ascii_isspace(*q))
            q++;
        if (q > p + 7)
        {
            GtkTextIter istart, iend;
            gtk_text_buffer_get_iter_at_offset(buffer, &istart,
                                               gtk_text_iter_get_offset(start) +
                                                   (p - text));
            gtk_text_buffer_get_iter_at_offset(buffer, &iend,
                                               gtk_text_iter_get_offset(start) +
                                                   (q - text));
            gchar *url = g_strndup(p, q - p);
            apply_link_tag(buffer, &istart, &iend, url);
            g_free(url);
        }
        p = q;
    }
    g_free(text);
}

/* Autoscroll to bottom of message list */
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

static void autoscroll_soon(void) { g_idle_add(autoscroll_idle_cb, NULL); }


static void copy_text_to_clipboard(const gchar *txt)
{
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, txt ? txt : "", -1);
}

static GtkWidget* make_row_container(void)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(row), outer);
    return row;
}

static void add_user_row(const gchar *text)
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
    autoscroll_soon();
}

/* Forward decl */
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

    GString  *carry;    /* JSON-lines (Ollama) */
    GString  *carry2;   /* SSE (OpenAI) */

    GtkWidget     *row;
    GtkWidget     *stream_view;
    GtkTextBuffer *stream_buf;

    GString *accum;
} Req;


static GtkWidget* add_assistant_stream_row(Req *req)
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
    autoscroll_soon();

    req->row = row;
    req->stream_view = tv;
    req->stream_buf  = buf;
    return row;
}

/* ----------- Code blocks (GtkSourceView) -------------------------------- */


static void insert_code_into_editor(GtkButton *b, gpointer data)
{
    (void)b;
    GtkTextBuffer *tb = GTK_TEXT_BUFFER(data);
    GtkTextIter a, z;
    gtk_text_buffer_get_bounds(tb, &a, &z);
    gchar *txt = gtk_text_buffer_get_text(tb, &a, &z, FALSE);

    GeanyDocument *doc = document_get_current();
    if (doc && doc->editor && doc->editor->sci && txt)
    {
        ScintillaObject *sci = doc->editor->sci;
        if (sci_has_selection(sci))
            sci_replace_sel(sci, txt);
        else
            sci_insert_text(sci, sci_get_current_position(sci), txt);

        gtk_widget_grab_focus(GTK_WIDGET(sci));
    }
    g_free(txt);
}

static void copy_code_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    GtkTextBuffer *tb = GTK_TEXT_BUFFER(data);
    GtkTextIter a, z;
    gtk_text_buffer_get_bounds(tb, &a, &z);
    gchar *txt = gtk_text_buffer_get_text(tb, &a, &z, FALSE);
    copy_text_to_clipboard(txt);
    g_free(txt);
}

static GtkSourceStyleScheme* suggested_scheme(void)
{
    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();
    GtkSourceStyleScheme *scheme = NULL;
    if (prefs.dark_theme)
    {
        scheme = gtk_source_style_scheme_manager_get_scheme(mgr, "oblivion");
        if (!scheme) scheme = gtk_source_style_scheme_manager_get_scheme(mgr, "cobalt");
    }
    else
    {
        scheme = gtk_source_style_scheme_manager_get_scheme(mgr, "classic");
        if (!scheme) scheme = gtk_source_style_scheme_manager_get_scheme(mgr, "tango");
    }
    if (!scheme)
        scheme = gtk_source_style_scheme_manager_get_scheme(mgr, "kate");
    return scheme;
}

/* -------------------------- Theming (light/dark) ------------------------- */

static GtkCssProvider *g_theme_provider = NULL;

static void apply_theme_css(void)
{
    if (!g_theme_provider)
        g_theme_provider = gtk_css_provider_new();

    const gchar *css_dark =
        ".ai-chat { background: #1e1e1e; }\n"
        ".ai-chat label { color: #e6e6e6; }\n"
        ".ai-chat .blockquote { border-left: 3px solid #4a4a4a; padding-left: 6px; }\n"
        ".ai-chat .code { background: #151515; }\n";

    const gchar *css_light =
        ".ai-chat { }\n"
        ".ai-chat .blockquote { border-left: 3px solid #c9c9c9; padding-left: 6px; }\n"
        ".ai-chat .code { background: #f7f7f7; }\n";

    const gchar *css = prefs.dark_theme ? css_dark : css_light;
    gtk_css_provider_load_from_data(g_theme_provider, css, -1, NULL);

    GdkScreen *screen = gdk_screen_get_default();
    if (screen)
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(g_theme_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void update_code_schemes_in_widget(GtkWidget *w)
{
    if (!GTK_IS_WIDGET(w)) return;
    /* Update this widget if it is a GtkSourceView */
    if (GTK_SOURCE_IS_VIEW(w))
    {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
        GtkSourceStyleScheme *scheme = suggested_scheme();
        if (GTK_SOURCE_IS_BUFFER(buf))
            gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buf), scheme);
    }
    /* Recurse into containers */
    if (GTK_IS_CONTAINER(w))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *l = children; l; l = l->next)
            update_code_schemes_in_widget(GTK_WIDGET(l->data));
        g_list_free(children);
    }
}

static void on_toggle_dark(GtkToggleButton *tb, gpointer user_data)
{
    (void)user_data;
    prefs.dark_theme = gtk_toggle_button_get_active(tb);
    prefs_save();
    apply_theme_css();
    update_code_schemes_in_widget(ui.msg_list);
}

/* Heuristic language detection for unlabeled code fences */
static const gchar* guess_lang_id(const gchar *code)
{
    if (!code) return NULL;
    /* limit scanning to first ~2000 bytes for speed */
    const gchar *p = code;
    gsize scanned = 0;

    /* Skip leading whitespace */
    while (*p && g_ascii_isspace(*p)) p++, scanned++;

    /* Shebang */
    if (g_str_has_prefix(p, "#!"))
    {
        if (strstr(p, "python")) return "python";
        if (strstr(p, "bash") || strstr(p, "sh")) return "sh";
        if (strstr(p, "node")) return "javascript";
        if (strstr(p, "perl")) return "perl";
        if (strstr(p, "ruby")) return "ruby";
    }

    /* XML / HTML / PHP */
    if (g_str_has_prefix(p, "<?xml")) return "xml";
    if (g_str_has_prefix(p, "<?php") || strstr(p, "<?php")) return "php";
    if (g_str_has_prefix(p, "<!DOCTYPE html") || g_str_has_prefix(p, "<html") || strstr(p, "</html>")) return "html";
    if (g_str_has_prefix(p, "<svg")) return "xml";

    /* JSON: starts with { or [ and has quotes/colon pairs */
    if (*p == '{' || *p == '[')
    {
        const gchar *q = p;
        int quotes = 0, colons = 0;
        for (; *q && scanned < 2000; ++q, ++scanned)
        {
            if (*q == '"') quotes++;
            else if (*q == ':') colons++;
            else if (*q == '\n') break;
        }
        if (quotes >= 2 && colons >= 1)
            return "json";
    }

    /* SQL */
    if (g_strrstr_len(p, 2000, "SELECT ") || g_strrstr_len(p, 2000, "INSERT INTO") ||
        g_strrstr_len(p, 2000, "CREATE TABLE") || g_strrstr_len(p, 2000, "UPDATE ") ||
        g_strrstr_len(p, 2000, "DELETE FROM"))
        return "sql";

    /* Go */
    if (g_strrstr_len(p, 2000, "package main") || g_strrstr_len(p, 2000, "func main("))
        return "go";

    /* Rust */
    if (g_strrstr_len(p, 2000, "fn main()") || g_strrstr_len(p, 2000, "println!(") ||
        g_strrstr_len(p, 2000, "let mut "))
        return "rust";

    /* Java */
    if (g_strrstr_len(p, 2000, "public class ") || g_strrstr_len(p, 2000, "System.out.println") ||
        g_strrstr_len(p, 2000, "import java."))
        return "java";

    /* C# */
    if (g_strrstr_len(p, 2000, "using System;") && g_strrstr_len(p, 2000, "namespace "))
        return "c-sharp";

    /* Python */
    if (g_strrstr_len(p, 2000, "def ") || g_strrstr_len(p, 2000, "import ") ||
        g_strrstr_len(p, 2000, "print("))
        return "python";

    /* JavaScript / TypeScript */
    if (g_strrstr_len(p, 2000, "console.log") || g_strrstr_len(p, 2000, "function ") ||
        g_strrstr_len(p, 2000, "=>") || g_strrstr_len(p, 2000, "import ") ||
        g_strrstr_len(p, 2000, "export "))
        return "javascript";

    /* Shell */
    if (g_strrstr_len(p, 2000, "#!/bin/sh") || g_strrstr_len(p, 2000, "#!/bin/bash") ||
        g_strrstr_len(p, 2000, "export ") || g_strrstr_len(p, 2000, "sudo ") ||
        g_strrstr_len(p, 2000, "apt ") || g_strrstr_len(p, 2000, "yum ") ||
        g_strrstr_len(p, 2000, "echo "))
        return "sh";

    /* CSS */
    if (g_strrstr_len(p, 2000, ": ") && g_strrstr_len(p, 2000, "{") && g_strrstr_len(p, 2000, "}"))
    {
        if (g_strrstr_len(p, 2000, "color:") || g_strrstr_len(p, 2000, "font-") || g_strrstr_len(p, 2000, "margin:"))
            return "css";
    }

    /* YAML */
    if (g_strrstr_len(p, 2000, ": ") && g_strrstr_len(p, 2000, "\n-") && !g_strrstr_len(p, 2000, "{"))
        return "yaml";

    /* TOML */
    if (g_str_has_prefix(p, "[") && g_strrstr_len(p, 2000, "]") && g_strrstr_len(p, 2000, " = "))
        return "toml";

    /* Dockerfile */
    if (g_str_has_prefix(p, "FROM ") || g_str_has_prefix(p, "RUN ") || g_str_has_prefix(p, "CMD "))
        return "docker";

    /* Lua */
    if (g_strrstr_len(p, 2000, "function ") && g_strrstr_len(p, 2000, " end") )
        return "lua";

    /* PHP already handled above */

    /* C / C++: includes / main */
    if (g_strrstr_len(p, 2000, "#include "))
    {
        if (g_strrstr_len(p, 2000, "<iostream>") || g_strrstr_len(p, 2000, "std::") || g_strrstr_len(p, 2000, "using namespace "))
            return "cpp";
        return "c";
    }
    if (g_strrstr_len(p, 2000, "int main(") || g_strrstr_len(p, 2000, "printf("))
        return "c";

    /* Fallbacks */
    return NULL;
}

static GtkWidget* create_code_block_widget(const gchar *code, const gchar *lang_hint)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *lab = gtk_label_new(lang_hint && *lang_hint ? lang_hint : "code");
    gtk_label_set_xalign(GTK_LABEL(lab), 0.0);
    GtkWidget *btn_copy = gtk_button_new_with_label("Copier");
    GtkWidget *btn_ins  = gtk_button_new_with_label("Insérer dans l'éditeur");
    gtk_box_pack_start(GTK_BOX(bar), lab, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(bar), btn_ins, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bar), btn_copy, FALSE, FALSE, 0);

    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceStyleScheme *scheme = suggested_scheme();
    GtkSourceLanguage *lang = NULL;
    if (lang_hint && *lang_hint)
    {
        lang = gtk_source_language_manager_get_language(lm, lang_hint);
        if (!lang)
        {
            gchar *lc = g_ascii_strdown(lang_hint, -1);
            lang = gtk_source_language_manager_get_language(lm, lc);
            g_free(lc);
        }
    }

    GtkWidget *view = gtk_source_view_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(view), "code");
    GtkTextBuffer *sbuf = gtk_text_buffer_new(NULL);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 8);

    if (!lang)
    {
        const gchar *id = guess_lang_id(code);
        if (id)
            lang = gtk_source_language_manager_get_language(lm, id);
    }
    if (lang)
        gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(sbuf), lang);
    if (scheme)
        gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(sbuf), scheme);

    /* Trim trailing newlines */
gchar *code_clean = g_strdup(code ? code : "");
g_strchomp(code_clean);
gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sbuf), code_clean, -1);
g_free(code_clean);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(view), sbuf);

    g_signal_connect(btn_copy, "clicked", G_CALLBACK(copy_code_clicked), sbuf);
    g_signal_connect(btn_ins,  "clicked", G_CALLBACK(insert_code_into_editor), sbuf);

    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), view, FALSE, FALSE, 0);
    return box;
}

static GtkWidget* build_assistant_composite_from_markdown(const gchar *text)
{
    GtkWidget *make_paragraph_label(const gchar *ptext)
    {
        GtkWidget *lbl = gtk_label_new(NULL);
        gchar *markup_txt = mk_markup_with_links(ptext);
        gtk_label_set_markup(GTK_LABEL(lbl), markup_txt);
        g_free(markup_txt);
        gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
        gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
        g_signal_connect(lbl, "activate-link", G_CALLBACK(on_label_activate_link), NULL);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
        gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
        return lbl;
    }

    GtkWidget *make_blockquote(const gchar *qtext)
    {
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_style_context_add_class(gtk_widget_get_style_context(hbox), "blockquote");
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_margin_top(sep, 2);
        gtk_widget_set_margin_bottom(sep, 2);
        gtk_box_pack_start(GTK_BOX(hbox), sep, FALSE, FALSE, 0);

        GtkWidget *lbl = make_paragraph_label(qtext);
        gtk_widget_set_margin_start(lbl, 6);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);
        return hbox;
    }

    void pack_with_blockquotes(GtkWidget *box, const gchar *segment)
    {
        const gchar *s = segment ? segment : "";
        GString *acc = g_string_new(NULL);
        gboolean in_quote = FALSE;

        while (*s)
        {
            const gchar *line_end = strchr(s, '\n');
            gsize len = line_end ? (gsize)(line_end - s) : strlen(s);

            /* Detect blockquote line: optional spaces then '>' */
            const gchar *t = s;
            while (t < s + len && (*t == ' ' || *t == '\t')) t++;
            gboolean is_quote = (t < s + len && *t == '>');

            if (is_quote) {
                t++;
                if (t < s + len && *t == ' ') t++;
            }

            if (is_quote != in_quote)
            {
                if (acc->len > 0)
                {
                    GtkWidget *w = in_quote ? make_blockquote(acc->str) : make_paragraph_label(acc->str);
                    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
                    g_string_set_size(acc, 0);
                }
                in_quote = is_quote;
            }

            if (is_quote)
                g_string_append_len(acc, t, (s + len) - t);
            else
                g_string_append_len(acc, s, len);

            /* preserve original newlines */
            if (line_end)
                g_string_append_c(acc, '\n');

            s = line_end ? line_end + 1 : s + len;
        }

        if (acc->len > 0)
        {
            GtkWidget *w = in_quote ? make_blockquote(acc->str) : make_paragraph_label(acc->str);
            gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
        }
        g_string_free(acc, TRUE);
    }

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *hdr = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>Assistant</b>");
    gtk_label_set_markup(GTK_LABEL(hdr), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(hdr), 0.0);
    gtk_box_pack_start(GTK_BOX(outer), hdr, FALSE, FALSE, 0);

    const gchar *p = text;
    while (p && *p)
    {
        const gchar *f = strstr(p, "```");
        if (!f)
        {
            if (*p)
                pack_with_blockquotes(outer, p);
            break;
        }

        if (f > p)
        {
            gchar *para = g_strndup(p, f - p);
            pack_with_blockquotes(outer, para);
            g_free(para);
        }

        const gchar *lang_start = f + 3;
        const gchar *nl = strchr(lang_start, '\n');
        if (!nl) break;
        gchar *lang = g_strstrip(g_strndup(lang_start, nl - lang_start));

        const gchar *end = strstr(nl + 1, "```");
        if (!end) end = text + strlen(text);
        gchar *code = g_strndup(nl + 1, end - (nl + 1));

        GtkWidget *codew = create_code_block_widget(code, lang);
        gtk_box_pack_start(GTK_BOX(outer), codew, FALSE, FALSE, 0);

        g_free(lang);
        g_free(code);
        p = (*end) ? end + 3 : end;
    }

    return outer;
}

static void replace_row_child(GtkWidget *row, GtkWidget *new_child)
{
    GtkWidget *old = gtk_bin_get_child(GTK_BIN(row));
    if (old) gtk_container_remove(GTK_CONTAINER(row), old);
    gtk_container_add(GTK_CONTAINER(row), new_child);
    gtk_widget_show_all(row);
    autoscroll_soon();
}

/* --------------------------- Historique --------------------------------- */

static gchar *history_json = NULL;

static gchar *json_escape(const gchar *s)
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

static void history_init(void)
{
    g_free(history_json);
    history_json = g_strdup("[]");
}

static void history_add(const gchar *role, const gchar *content)
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

/* ------------------------------ Réseau ---------------------------------- */

struct Mem { char *data; size_t size; };

static Req *current_req = NULL;

static size_t collect_cb(void *ptr, size_t size, size_t nm, void *ud)
{
    size_t r = size * nm;
    struct Mem *m = (struct Mem *)ud;
    char *p = g_realloc(m->data, m->size + r + 1);
    if (!p) return 0;
    m->data = p;
    memcpy(m->data + m->size, ptr, r);
    m->size += r;
    m->data[m->size] = 0;
    return r;
}

/* --- Safe idle append --------------------------------------------------- */

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
        autoscroll_soon();
    }
    g_free(ctx->text);
    g_free(ctx);
    return FALSE;
}

static void ui_stream_append_req(Req *req, const char *s, gssize len)
{
    if (!s) return;
    AppendCtx *ctx = g_new0(AppendCtx, 1);
    ctx->buf  = req->stream_buf;
    ctx->text = len >= 0 ? g_strndup(s, (gsize)len) : g_strdup(s);
    g_idle_add(append_idle_cb, ctx);
}

/* --- Final replace row idle -------------------------------------------- */

typedef struct {
    GtkWidget *row;
    gchar     *final_text;
} ReplaceCtx;

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

/* --- Busy toggle idle --------------------------------------------------- */

static gboolean set_busy_idle_cb(gpointer data)
{
    gboolean on = GPOINTER_TO_INT(data);
    ui.busy = on;
    gtk_widget_set_sensitive(ui.btn_send,     !on);
    gtk_widget_set_sensitive(ui.btn_send_sel, !on);
    gtk_widget_set_sensitive(ui.btn_clear,    !on);
    gtk_widget_set_sensitive(ui.btn_reset,    !on);
    gtk_widget_set_sensitive(ui.btn_copy_all, !on);
    gtk_widget_set_sensitive(ui.btn_stop,      on);
    return FALSE;
}

/* --- Streaming parsing -------------------------------------------------- */
/* --- JSON string unescape (UTF-8) ------------------------------------- */

static void gstring_append_utf8_cp(GString *dst, guint32 cp)
{
    if (cp <= 0x7F)
        g_string_append_c(dst, (gchar)cp);
    else if (cp <= 0x7FF)
    {
        g_string_append_c(dst, (gchar)(0xC0 | ((cp >> 6) & 0x1F)));
        g_string_append_c(dst, (gchar)(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        g_string_append_c(dst, (gchar)(0xE0 | ((cp >> 12) & 0x0F)));
        g_string_append_c(dst, (gchar)(0x80 | ((cp >> 6) & 0x3F)));
        g_string_append_c(dst, (gchar)(0x80 | (cp & 0x3F)));
    }
    else
    {
        g_string_append_c(dst, (gchar)(0xF0 | ((cp >> 18) & 0x07)));
        g_string_append_c(dst, (gchar)(0x80 | ((cp >> 12) & 0x3F)));
        g_string_append_c(dst, (gchar)(0x80 | ((cp >> 6) & 0x3F)));
        g_string_append_c(dst, (gchar)(0x80 | (cp & 0x3F)));
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void json_unescape_append(GString *dst, const char *s, const char *e)
{
    const char *p = s;
    while (p < e)
    {
        if (*p != '\\')
        {
            g_string_append_c(dst, *p++);
            continue;
        }
        p++; if (p >= e) break;
        switch (*p)
        {
            case '\"': g_string_append_c(dst, '\"'); p++; break;
            case '\\': g_string_append_c(dst, '\\'); p++; break;
            case '/':  g_string_append_c(dst, '/');  p++; break;
            case 'b':  g_string_append_c(dst, '\b'); p++; break;
            case 'f':  g_string_append_c(dst, '\f'); p++; break;
            case 'n':  g_string_append_c(dst, '\n'); p++; break;
            case 'r':  g_string_append_c(dst, '\r'); p++; break;
            case 't':  g_string_append_c(dst, '\t'); p++; break;
            case 'u':
            {
                /* \uXXXX (handle surrogate pairs) */
                if (p + 4 >= e) { p = e; break; }
                int h1 = hexval(*(p+1));
                int h2 = hexval(*(p+2));
                int h3 = hexval(*(p+3));
                int h4 = hexval(*(p+4));
                if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) { p++; break; }
                guint32 u = (guint32)((h1<<12) | (h2<<8) | (h3<<4) | h4);
                p += 5;
                if (u >= 0xD800 && u <= 0xDBFF)
                {
                    /* high surrogate, expect \uDC00..DFFF */
                    if (p + 1 < e && *p == '\\' && *(p+1) == 'u' && p + 6 <= e)
                    {
                        int h5 = hexval(*(p+2));
                        int h6 = hexval(*(p+3));
                        int h7 = hexval(*(p+4));
                        int h8 = hexval(*(p+5));
                        guint32 u2 = (guint32)((h5<<12)|(h6<<8)|(h7<<4)|h8);
                        if (h5>=0 && h6>=0 && h7>=0 && h8>=0 &&
                            u2 >= 0xDC00 && u2 <= 0xDFFF)
                        {
                            guint32 cp = 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00));
                            gstring_append_utf8_cp(dst, cp);
                            p += 6;
                            break;
                        }
                    }
                    /* malformed pair: emit replacement char */
                    gstring_append_utf8_cp(dst, 0xFFFD);
                }
                else
                {
                    gstring_append_utf8_cp(dst, u);
                }
                break;
            }
            default:
                /* Unknown escape, keep char */
                g_string_append_c(dst, *p++);
                break;
        }
    }
}

static void append_decoded_segment(Req *req, const char *start, const char *end)
{
    GString *dec = g_string_new(NULL);
    json_unescape_append(dec, start, end);
    if (dec->len)
    {
        if (!req->accum) req->accum = g_string_new(NULL);
        g_string_append_len(req->accum, dec->str, dec->len);
        ui_stream_append_req(req, dec->str, (gssize)dec->len);
    }
    g_string_free(dec, TRUE);
}


static void accum_append(Req *req, const char *start, const char *end)
{
    if (!req->accum) req->accum = g_string_new(NULL);
    g_string_append_len(req->accum, start, end - start);
    ui_stream_append_req(req, start, end - start);
}

static gboolean extract_content_and_append(const char *line, size_t len, Req *req)
{
    const char *c = strstr(line, "\"content\"");
    if (!c || c >= line + len) return FALSE;
    c = strchr(c, ':');
    if (!c) return FALSE;
    c++;
    while (c < line + len && *c == ' ') c++;
    if (c >= line + len || *c != '\"') return FALSE;
    c++;
    const char *p = c;
    while (p < line + len)
    {
        if (*p == '"' && *(p - 1) != '\\') break;
        p++;
    }
    if (p > line + len) return FALSE;
    append_decoded_segment(req, c, p);
    return TRUE;
}

static size_t stream_cb_ollama(void *ptr, size_t size, size_t nm, void *ud)
{
    Req *req = (Req *)ud;
    if (g_atomic_int_get(&req->cancel)) return 0;
    size_t r = size * nm;
    if (!req->carry) req->carry = g_string_new(NULL);
    g_string_append_len(req->carry, (const gchar *)ptr, r);

    for (;;)
    {
        char *nl = memchr(req->carry->str, '\n', req->carry->len);
        if (!nl) break;
        size_t linelen = (size_t)(nl - req->carry->str);
        if (linelen > 0)
            extract_content_and_append(req->carry->str, linelen, req);
        g_string_erase(req->carry, 0, linelen + 1);
    }
    return r;
}

static size_t stream_cb_openai(void *ptr, size_t size, size_t nm, void *ud)
{
    Req *req = (Req *)ud;
    if (g_atomic_int_get(&req->cancel)) return 0;
    size_t r = size * nm;
    if (!req->carry2) req->carry2 = g_string_new(NULL);
    g_string_append_len(req->carry2, (const gchar *)ptr, r);

    for (;;)
    {
        char *start = strstr(req->carry2->str, "data:");
        if (!start) break;
        char *sep = strstr(start, "\n\n");
        if (!sep) break;

        size_t evlen = (size_t)(sep - start);
        if (!g_str_has_prefix(start + 5, " [DONE]"))
            extract_content_and_append(start, evlen, req);

        size_t erase_len = (sep - req->carry2->str) + 2;
        g_string_erase(req->carry2, 0, erase_len);
    }
    return r;
}

static int xferinfo_cb(void *clientp, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    Req *req = (Req *)clientp;
    if (g_atomic_int_get(&req->cancel)) return 1;
    return 0;
}

static gpointer net_thread(gpointer data)
{
    Req *req = (Req *)data;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        ui_stream_append_req(req, "[Erreur] curl init\n", -1);
        goto done;
    }

    gchar *url = NULL;
    gchar *payload = NULL;
    struct curl_slist *hdr = NULL;

    if (req->mode == API_OLLAMA)
    {
        url = g_strdup_printf("%s/api/chat", req->base);
        history_add("user", req->prompt);
        payload = g_strdup_printf("{\"model\":\"%s\",\"messages\":%s,"
                                  "\"stream\":%s,\"options\":{\"temperature\":%.3f}}",
                                  req->model, history_json,
                                  req->streaming ? "true" : "false", req->temp);
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
    }
    else
    {
        url = g_strdup_printf("%s/v1/chat/completions", req->base);
        gchar *esc = json_escape(req->prompt);
        payload = g_strdup_printf("{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
                                  "\"content\":\"%s\"}],\"temperature\":%.3f,"
                                  "\"stream\":%s}",
                                  req->model, esc, req->temp,
                                  req->streaming ? "true" : "false");
        g_free(esc);
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
        if (req->api_key && *req->api_key)
        {
            gchar *auth = g_strdup_printf("Authorization: Bearer %s", req->api_key);
            hdr = curl_slist_append(hdr, auth);
            g_free(auth);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, req);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    if (req->streaming)
    {
        if (req->mode == API_OLLAMA)
        {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_cb_ollama);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_cb_openai);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
        }
        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_ABORTED_BY_CALLBACK)
            ui_stream_append_req(req, "\n[Annulé]\n", -1);
        else if (rc != CURLE_OK)
            ui_stream_append_req(req, "\n[Erreur] streaming\n", -1);
    }
    else
    {
        struct Mem mem = {0};
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_ABORTED_BY_CALLBACK)
            ui_stream_append_req(req, "\n[Annulé]\n", -1);
        else if (rc != CURLE_OK)
            ui_stream_append_req(req, "\n[Erreur] requête\n", -1);
        else if (mem.data && mem.size)
        {
            extract_content_and_append(mem.data, mem.size, req);
        }
        g_free(mem.data);
    }

    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    g_free(url);
    g_free(payload);

done:
    /* Remplacer la bulle streaming par le rendu final */
    gchar *final = req->accum ? g_string_free(req->accum, FALSE) : g_strdup("");
    /* Ajoute la réponse de l'assistant à l'historique (utile pour Ollama) */
    if (final && *final)
        history_add("assistant", final);
    ReplaceCtx *rc = g_new0(ReplaceCtx, 1);
    rc->row = req->row;
    rc->final_text = final;
    g_idle_add(replace_row_idle_cb, rc);

    /* UI ready */
    g_idle_add(set_busy_idle_cb, GINT_TO_POINTER(FALSE));

    g_free(req->prompt);
    g_free(req->base);
    g_free(req->model);
    g_free(req->api_key);
    if (req->carry)  g_string_free(req->carry, TRUE);
    if (req->carry2) g_string_free(req->carry2, TRUE);
    current_req = NULL;
    g_free(req);
    return NULL;
}

/* -------------------------- Actions & UI -------------------------------- */

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

static gboolean on_input_key(GtkWidget *w, GdkEventKey *e, gpointer u)
{
    (void)w; (void)u;
    if (e->keyval == GDK_KEY_Return && !(e->state & GDK_SHIFT_MASK))
    {
        if (!ui.busy)
            g_signal_emit_by_name(ui.btn_send, "clicked");
        return TRUE;
    }
    return FALSE;
}

static void read_prefs_from_ui(ApiMode *mode, gchar **base, gchar **model,
                               gdouble *temp, gchar **key, gboolean *stream)
{
    *mode  = (ApiMode) gtk_combo_box_get_active(GTK_COMBO_BOX(ui.cmb_api));
    *base  = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui.ent_url)));
    *model = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui.ent_model)));
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

static void send_prompt(const gchar *prompt)
{
    if (!prompt || !*prompt) return;

    ApiMode mode; gchar *base; gchar *model; gdouble temp;
    gchar *key; gboolean stream;
    read_prefs_from_ui(&mode, &base, &model, &temp, &key, &stream);
    save_prefs_from_vals(mode, base, model, temp, key, stream);

    add_user_row(prompt);

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

    add_assistant_stream_row(req);

    current_req = req;
    g_idle_add(set_busy_idle_cb, GINT_TO_POINTER(TRUE));
    g_thread_new("ai_chat_http", net_thread, req);
}

static void on_send(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    if (ui.busy) return; /* ignore while streaming */
    GtkTextIter a, z;
    gtk_text_buffer_get_bounds(ui.input_buf, &a, &z);
    gchar *prompt = gtk_text_buffer_get_text(ui.input_buf, &a, &z, FALSE);
    send_prompt(prompt);
    gtk_text_buffer_set_text(ui.input_buf, "", -1);
    g_free(prompt);
}

static void on_send_selection(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->editor || !doc->editor->sci)
    {
        GtkWidget *row = make_row_container();
        GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));
        GtkWidget *lbl = gtk_label_new("[Info] Aucun document actif.");
        gtk_box_pack_start(GTK_BOX(outer), lbl, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
        gtk_widget_show_all(row);
    autoscroll_soon();
    autoscroll_soon();
        return;
    }
    ScintillaObject *sci = doc->editor->sci;
    if (!sci_has_selection(sci))
    {
        GtkWidget *row = make_row_container();
        GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));
        GtkWidget *lbl = gtk_label_new("[Info] Aucune sélection.");
        gtk_box_pack_start(GTK_BOX(outer), lbl, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
        gtk_widget_show_all(row);
    autoscroll_soon();
    autoscroll_soon();
        return;
    }
    gchar *sel = sci_get_selection_contents(sci);
    if (!sel || !*sel)
    {
        g_free(sel);
        return;
    }
    send_prompt(sel);
    g_free(sel);
}

static void on_clear(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    GList *children = gtk_container_get_children(GTK_CONTAINER(ui.msg_list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
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
#ifdef GTK_SOURCE_VIEW_H
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
#endif
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
    copy_text_to_clipboard(out->str);
    g_string_free(out, TRUE);
}

static void on_reset(GtkButton *b, gpointer u)
{
    (void)b; (void)u;
    history_init();
    GtkWidget *row = make_row_container();
    GtkWidget *outer = gtk_bin_get_child(GTK_BIN(row));
    GtkWidget *lbl = gtk_label_new("[Historique réinitialisé]");
    gtk_box_pack_start(GTK_BOX(outer), lbl, FALSE, FALSE, 0);
    gtk_list_box_insert(GTK_LIST_BOX(ui.msg_list), row, -1);
    gtk_widget_show_all(row);
    autoscroll_soon();
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

/* -------------------------- UI construction ----------------------------- */

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

static void build_ui(void)
{
    GtkWidget *nb = g_plugin->geany_data->main_widgets->message_window_notebook;

    ui.root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.root_box), "ai-chat");
    apply_theme_css();

    GtkWidget *opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui.cmb_api = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cmb_api), "Ollama");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cmb_api), "OpenAI-compat");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.cmb_api), prefs.api_mode == API_OPENAI ? 1 : 0);

    GtkWidget *url_box = make_labeled_entry("URL", &ui.ent_url);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_url), prefs.base_url);

    GtkWidget *model_box = make_labeled_entry("Modèle", &ui.ent_model);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_model), prefs.model);

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

    GtkWidget *key_box = make_labeled_entry("Clé", &ui.ent_key);
    gtk_entry_set_text(GTK_ENTRY(ui.ent_key), prefs.api_key);

    gtk_box_pack_start(GTK_BOX(opts), ui.cmb_api, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), url_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(opts), model_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(opts), temp_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.chk_stream, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), ui.chk_dark, FALSE, FALSE, 0);
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
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(input_scroll), 64);
    ui.input_view = gtk_text_view_new();
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

    g_signal_connect(ui.btn_send,     "clicked", G_CALLBACK(on_send), NULL);
    g_signal_connect(ui.btn_send_sel, "clicked", G_CALLBACK(on_send_selection), NULL);
    g_signal_connect(ui.btn_stop,     "clicked", G_CALLBACK(on_stop), NULL);
    g_signal_connect(ui.btn_clear,    "clicked", G_CALLBACK(on_clear), NULL);
    g_signal_connect(ui.btn_reset,    "clicked", G_CALLBACK(on_reset), NULL);
    g_signal_connect(ui.btn_copy_all, "clicked", G_CALLBACK(on_copy_all), NULL);

    gtk_box_pack_start(GTK_BOX(btns), ui.btn_send,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_send_sel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_stop,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_clear,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_reset,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), ui.btn_copy_all, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(ui.root_box), opts,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), scroll, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), input_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.root_box), btns,   FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), ui.root_box, gtk_label_new("Chat IA"));
    gtk_widget_show_all(ui.root_box);

    gtk_widget_set_sensitive(ui.btn_stop, FALSE);
}

/* -------------------------- Lifecycle (modern) -------------------------- */

static gboolean my_plugin_init(GeanyPlugin *plugin, gpointer data)
{
    (void)data;
    g_plugin = plugin;
    prefs_load();
    if (!prefs.base_url) prefs_set_defaults();
    history_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    build_ui();
    return TRUE;
}

static void my_plugin_cleanup(GeanyPlugin *plugin, gpointer data)
{
    (void)plugin; (void)data;
    prefs_save();
    curl_global_cleanup();
    g_clear_pointer(&history_json, g_free);
    prefs_free();
}

static PluginCallback callbacks[] = { { NULL, NULL, FALSE, NULL } };

G_MODULE_EXPORT
void geany_load_module(GeanyPlugin *plugin)
{
    plugin->info->name        = "AI Chat (pro)";
    plugin->info->description = "Chat local (Ollama/OpenAI) avec streaming, "
                                "sélection éditeur, Stop et code blocks colorés.";
    plugin->info->version     = "1.7";
    plugin->info->author      = "Toi";

    plugin->funcs->init      = my_plugin_init;
    plugin->funcs->cleanup   = my_plugin_cleanup;
    plugin->funcs->callbacks = callbacks;

    GEANY_PLUGIN_REGISTER(plugin, GEANY_API_VERSION);
}
