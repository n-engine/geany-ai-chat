/*
 * ui_render.c — Markdown rendering, code blocks, links for AI Chat plugin
 */

#include "ui_render.h"
#include "prefs.h"
#include <geanyplugin.h>
#include <string.h>

/* Forward declarations for nested helper functions */
static GtkWidget* make_paragraph_label(const gchar *ptext);
static GtkWidget* make_blockquote(const gchar *qtext);
static void pack_with_blockquotes(GtkWidget *box, const gchar *segment);

/* --- Clickable links in GtkLabel ----------------------------------------- */

gboolean on_label_activate_link(GtkLabel *label, const gchar *uri, gpointer user_data)
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
    return TRUE;
}

/* --- URL character detection --------------------------------------------- */

static inline gboolean is_url_char(gunichar c)
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

static void append_escaped(GString *out, const gchar *s, gssize len)
{
    if (len < 0) len = (gssize)strlen(s);
    gchar *esc = g_markup_escape_text(s, (gssize)len);
    g_string_append(out, esc);
    g_free(esc);
}

/* --- Markup with links --------------------------------------------------- */

gchar* mk_markup_with_links(const gchar *src)
{
    if (!src) return g_strdup("");

    /* If links are disabled, just escape and return */
    if (!prefs.links_enabled)
        return g_markup_escape_text(src, -1);

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

        /* Markdown link: [label](url) */
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

        /* Bare URLs */
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

/* --- Theme CSS ----------------------------------------------------------- */

static GtkCssProvider *g_theme_provider = NULL;

void apply_theme_css(void)
{
    if (!g_theme_provider)
        g_theme_provider = gtk_css_provider_new();

    const gchar *css_dark =
        ".ai-chat { background-color: #1e1e1e; }\n"
        ".ai-chat label { color: #e6e6e6; }\n"
        ".ai-chat .blockquote { background-color: rgba(255,255,255,0.03); border-left: 3px solid #555; padding: 6px 10px; border-radius: 0 4px 4px 0; }\n"
        ".ai-chat .code { background-color: #121212; border: 1px solid #333; border-radius: 4px; padding: 6px 8px; }\n"
        ".ai-chat .input-wrap { background-color: #222; border: 1px solid #333; border-radius: 6px; }\n"
        ".ai-chat textview.input, .ai-chat textview.input text { background-color: #1b1b1b; color: #e6e6e6; caret-color: #f0f0f0; }\n";

    const gchar *css_light =
        ".ai-chat { }\n"
        ".ai-chat .blockquote { background-color: rgba(0,0,0,0.03); border-left: 3px solid #aaa; padding: 6px 10px; border-radius: 0 4px 4px 0; }\n"
        ".ai-chat .code { background-color: #f1f3f5; border: 1px solid #ddd; border-radius: 4px; padding: 6px 8px; }\n"
        ".ai-chat .input-wrap { background-color: #ffffff; border: 1px solid #ddd; border-radius: 6px; }\n"
        ".ai-chat textview.input, .ai-chat textview.input text { background-color: #fafafa; color: #111; caret-color: #111; }\n";

    const gchar *css = prefs.dark_theme ? css_dark : css_light;
    gtk_css_provider_load_from_data(g_theme_provider, css, -1, NULL);

    GdkScreen *screen = gdk_screen_get_default();
    if (screen)
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(g_theme_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* --- Color schemes ------------------------------------------------------- */

GtkSourceStyleScheme* suggested_scheme(void)
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

void update_code_schemes_in_widget(GtkWidget *w)
{
    if (!GTK_IS_WIDGET(w)) return;
    if (GTK_SOURCE_IS_VIEW(w))
    {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
        GtkSourceStyleScheme *scheme = suggested_scheme();
        if (GTK_SOURCE_IS_BUFFER(buf))
            gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buf), scheme);
    }
    if (GTK_IS_CONTAINER(w))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *l = children; l; l = l->next)
            update_code_schemes_in_widget(GTK_WIDGET(l->data));
        g_list_free(children);
    }
}

/* --- Language detection -------------------------------------------------- */

const gchar* guess_lang_id(const gchar *code)
{
    if (!code) return NULL;
    const gchar *p = code;
    gsize scanned = 0;

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

    /* JSON */
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

    /* C / C++ */
    if (g_strrstr_len(p, 2000, "#include "))
    {
        if (g_strrstr_len(p, 2000, "<iostream>") || g_strrstr_len(p, 2000, "std::") || g_strrstr_len(p, 2000, "using namespace "))
            return "cpp";
        return "c";
    }
    if (g_strrstr_len(p, 2000, "int main(") || g_strrstr_len(p, 2000, "printf("))
        return "c";

    return NULL;
}

/* --- Code block callbacks (external, needs Geany) ------------------------ */

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
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, txt ? txt : "", -1);
    g_free(txt);
}

/* --- Code block widget --------------------------------------------------- */

GtkWidget* create_code_block_widget(const gchar *code, const gchar *lang_hint)
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
    GtkTextBuffer *sbuf = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
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

/* --- Helper functions for composite building ----------------------------- */

static GtkWidget* make_paragraph_label(const gchar *ptext)
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
    /* Add vertical margins for better paragraph spacing */
    gtk_widget_set_margin_top(lbl, 3);
    gtk_widget_set_margin_bottom(lbl, 3);
    return lbl;
}

static GtkWidget* make_blockquote(const gchar *qtext)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(hbox), "blockquote");
    /* Add vertical margins to blockquote container */
    gtk_widget_set_margin_top(hbox, 4);
    gtk_widget_set_margin_bottom(hbox, 4);
    gtk_widget_set_margin_start(hbox, 8);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_top(sep, 2);
    gtk_widget_set_margin_bottom(sep, 2);
    gtk_box_pack_start(GTK_BOX(hbox), sep, FALSE, FALSE, 0);

    GtkWidget *lbl = make_paragraph_label(qtext);
    /* Reset paragraph margins inside blockquote to avoid double spacing */
    gtk_widget_set_margin_top(lbl, 0);
    gtk_widget_set_margin_bottom(lbl, 0);
    gtk_widget_set_margin_start(lbl, 6);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);
    return hbox;
}

static void pack_with_blockquotes(GtkWidget *box, const gchar *segment)
{
    const gchar *s = segment ? segment : "";
    GString *acc = g_string_new(NULL);
    gboolean in_quote = FALSE;

    while (*s)
    {
        const gchar *line_end = strchr(s, '\n');
        gsize len = line_end ? (gsize)(line_end - s) : strlen(s);

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

/* --- Build composite from markdown --------------------------------------- */

GtkWidget* build_assistant_composite_from_markdown(const gchar *text)
{
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
