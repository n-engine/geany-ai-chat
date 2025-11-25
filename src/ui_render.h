/*
 * ui_render.h — Markdown rendering, code blocks, links for AI Chat plugin
 */

#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

/* Convert text with Markdown links and bare URLs to Pango markup */
gchar* mk_markup_with_links(const gchar *src);

/* Signal handler for clickable links in GtkLabel */
gboolean on_label_activate_link(GtkLabel *label, const gchar *uri, gpointer user_data);

/* Create a code block widget with syntax highlighting */
GtkWidget* create_code_block_widget(const gchar *code, const gchar *lang_hint);

/* Build composite widget from markdown text (text + code blocks + blockquotes) */
GtkWidget* build_assistant_composite_from_markdown(const gchar *text);

/* Get suggested color scheme based on dark/light theme */
GtkSourceStyleScheme* suggested_scheme(void);

/* Heuristic language detection for unlabeled code fences */
const gchar* guess_lang_id(const gchar *code);

/* Apply CSS theme (dark/light) */
void apply_theme_css(void);

/* Update code block color schemes in widget tree */
void update_code_schemes_in_widget(GtkWidget *w);

#endif /* UI_RENDER_H */
