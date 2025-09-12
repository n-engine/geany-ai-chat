/*
 * ai\_chat.c — Geany plugin (geany\_load\_module API)
 * Chat IA: streaming, sélection éditeur, Stop,
 * GtkSourceView pour code blocks, sans trampolines GCC (no execstack)
 *
 * Build (GtkSourceView 3.x) :
 *   gcc -fPIC -shared -O2 -Wall -Wextra \
 *     $(pkg-config --cflags geany gtk+-3.0 gtksourceview-3.0) \
 *     -o ai\_chat.so ai\_chat.c \
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

static GeanyPlugin *g\_plugin = NULL;

/* ---------------------------- Prefs ------------------------------------- */

typedef enum
{
    API\_OLLAMA = 0,
    API\_OPENAI = 1
} ApiMode;

typedef struct
{
    ApiMode  api\_mode;
    gchar   *base\_url;
    gchar   *model;
    gdouble  temperature;
    gchar   *api\_key;
    gboolean streaming;
    gboolean dark\_theme;
    gchar   *system\_prompt;
} AiPrefs;

static AiPrefs prefs;
static gchar  *conf\_path = NULL;

static void prefs\_set\_defaults(void)
{
    prefs.api\_mode    = API\_OLLAMA;
    prefs.base\_url    = g\_strdup("http://127.0.0.1:11434");
    prefs.model       = g\_strdup("llama3:8b");
    prefs.temperature = 0.2;
    prefs.api\_key     = g\_strdup("");
    prefs.streaming   = TRUE;
    prefs.dark\_theme  = FALSE;
    prefs.system\_prompt = g\_strdup("");
}

static void prefs\_free(void)
{
    g\_clear\_pointer(&prefs.base\_url, g\_free);
    g\_clear\_pointer(&prefs.model,    g\_free);
    g\_clear\_pointer(&prefs.api\_key,  g\_free);
    g\_clear\_pointer(&prefs.system\_prompt, g\_free);
}

static void prefs\_load(void)
{
    GKeyFile *kf = g\_key\_file\_new();
    GError *err  = NULL;

    g\_free(conf\_path);
    conf\_path = g\_build\_filename(g\_get\_user\_config\_dir(),
                                     "geany", "ai\_chat.conf", NULL);

    if (!g\_key\_file\_load\_from\_file(kf, conf\_path, G\_KEY\_FILE\_NONE, &err))
    {
        g\_clear\_error(&err);
        prefs\_set\_defaults();
        g\_key\_file\_free(kf);
        return;
    }

    prefs.api\_mode = (ApiMode) g\_key\_file\_get\_integer(kf, "chat", "api\_mode", NULL);
    if (prefs.api\_mode != API\_OLLAMA && prefs.api\_mode != API\_OPENAI)
        prefs.api\_mode = API\_OLLAMA;

    g\_free(prefs.base\_url);
    prefs.base\_url = g\_key\_file\_get\_string(kf, "chat", "base\_url", NULL);
    if (!prefs.base\_url) prefs.base\_url = g\_strdup("http://127.0.0.1:11434");

    g\_free(prefs.model);
    prefs.model = g\_key\_file\_get\_string(kf, "chat", "model", NULL);
    if (!prefs.model) prefs.model = g\_strdup("llama3:8b");

    prefs.temperature = g\_key\_file\_get\_double(kf, "chat", "temperature", NULL);
    if (prefs.temperature < 0.0 || prefs.temperature > 1.0) prefs.temperature = 0.2;

    g\_free(prefs.api\_key);
    prefs.api\_key = g\_key\_file\_get\_string(kf, "chat", "api\_key", NULL);
    if (!prefs.api\_key) prefs.api\_key = g\_strdup("");

    prefs.streaming = g\_key\_file\_get\_boolean(kf, "chat", "streaming", NULL);

    if (g\_key\_file\_has\_key(kf, "chat", "dark\_theme", NULL))
        prefs.dark\_theme = g\_key\_file\_get\_boolean(kf, "chat", "dark\_theme", NULL);
    else
        prefs.dark\_theme = FALSE;

    g\_free(prefs.system\_prompt);
    prefs.system\_prompt = g\_key\_file\_get\_string(kf, "chat", "system\_prompt", NULL);
    if (!prefs.system\_prompt) prefs.system\_prompt = g\_strdup("");

    g\_key\_file\_free(kf);
}

static void prefs\_save(void)
{
    GKeyFile *kf = g\_key\_file\_new();
    gchar *txt   = NULL;
    gsize  len   = 0;

    g\_key\_file\_set\_integer(kf, "chat", "api\_mode", prefs.api\_mode);
    g\_key\_file\_set\_string(kf,  "chat", "base\_url", prefs.base\_url);
    g\_key\_file\_set\_string(kf,  "chat", "model",    prefs.model);
    g\_key\_file\_set\_double(kf,  "chat", "temperature", prefs.temperature);
    g\_key\_file\_set\_string(kf,  "chat", "api\_key",  prefs.api\_key);
    g\_key\_file\_set\_boolean(kf, "chat", "streaming", prefs.streaming);
    g\_key\_file\_set\_boolean(kf, "chat", "dark\_theme", prefs.dark\_theme);
    g\_key\_file\_set\_string(kf,  "chat", "system\_prompt", prefs.system\_prompt);

    txt = g\_key\_file\_to\_data(kf, &len, NULL);
    g\_mkdir\_with\_parents(g\_path\_get\_dirname(conf\_path), 0700);
    g\_file\_set\_contents(conf\_path, txt, (gssize)len, NULL);
    g\_free(txt);
    g\_key\_file\_free(kf);
}

/* ------------------------------ UI ------------------------------------- */

typedef struct
{
    GtkWidget    *root\_box;

    GtkWidget    *msg\_list;      /* GtkListBox des messages */
    GtkWidget    *scroll;        /* Scrollable pour les messages */

    GtkWidget    *input\_row;     /* Ligne de saisie avec boutons */
    GtkWidget    *btn\_emoji;     /* Bouton emoji */
    GtkWidget    *input\_view;    /* Zone de texte pour la saisie */
    GtkTextBuffer *input\_buf;    /* Buffer pour la zone de texte */

    GtkWidget    *btns;          /* Widget contenant les boutons d'action */
    GtkWidget    *btn\_send;      /* Bouton envoyer */
    GtkWidget    *btn\_send\_sel;  /* Bouton envoyer sélection */
    GtkWidget    *btn\_stop;      /* Bouton stop */
    GtkWidget    *btn\_clear;     /* Bouton clear */
    GtkWidget    *btn\_reset;     /* Bouton reset */
    GtkWidget    *btn\_copy\_all;  /* Bouton copy all */
} Ui;

static Ui ui = {0};

/* -------------------------- Helpers ------------------------------------- */

/* --- Linkification (Pango <a href>) --- */

typedef struct
{
    gchar *text;
    gchar *link;
} LinkData;

static GRegex *re\_url = NULL;
static GRegex *re\_emoji = NULL;

/* Renvoyer TRUE si le texte contient un lien */
static gboolean has\_link(const gchar *text)
{
    return re\_url && g\_regex\_match(re\_url, text, 0, NULL);
}

/* Créer des liens dans le texte si possible */
static void linkify(GtkTextView *view, const gchar *text)
{
    if (!text || !*text) return;

    GtkTextBuffer *buf = gtk\_text\_view\_get\_buffer(view);
    GtkTextIter start, end;
    gtk\_text\_buffer\_get\_start\_iter(buf, &start);
    gtk\_text\_buffer\_get\_end\_iter(buf, &end);

    /* Supprimer les liens existants */
    if (has\_link(text))
        gtk\_text\_buffer\_remove\_all\_tags\_by\_name(buf, "ai-chat-link");

    /* Créer des liens dans le texte */
    GMatchInfo *match\_info = NULL;
    gchar **urls = g\_regex\_split(re\_url, text, 0);
    for (int i = 0; urls[i]; ++i)
    {
        if (i % 2 == 1) /* URL */
        {
            LinkData *link\_data = g\_new0(LinkData, 1);
            link\_data->text = g\_strndup(text, urls[i - 1] - text);
            link\_data->link = g\_strdup(urls[i]);

            GtkTextTag *tag = gtk\_text\_buffer\_create\_tag(buf, "ai-chat-link",
                                                             "foreground", "blue",
                                                             "underline", PANGO\_UNDERLINE\_SINGLE,
                                                             NULL);
            GtkTextIter it;
            gtk\_text\_buffer\_get\_iter\_at\_mark(buf, &it, gtk\_text\_buffer\_get\_insert(buf));
            gtk\_text\_buffer\_insert(buf, &it, link\_data->text, -1);
            gtk\_text\_buffer\_apply\_tag(buf, tag, &it, NULL);
            gtk\_text\_buffer\_insert(buf, &it, link\_data->link, -1);
            gtk\_text\_buffer\_apply\_tag(buf, tag, &it, NULL);
            gtk\_text\_buffer\_insert(buf, &it, "\n", 1);

            g\_free(link\_data->text);
            g\_free(link\_data->link);
            g\_free(link\_data);
        }
        else /* Texte normal */
        {
            GtkTextIter it;
            gtk\_text\_buffer\_get\_iter\_at\_mark(buf, &it, gtk\_text\_buffer\_get\_insert(buf));
            gtk\_text\_buffer\_insert(buf, &it, urls[i], -1);
        }
    }
    g\_strfreev(urls);
}

/* --- Emoji --- */

static GHashTable *emoji\_hash = NULL;

/* Renvoyer TRUE si le texte contient un emoji */
static gboolean has\_emoji(const gchar *text)
{
    return re\_emoji && g\_regex\_match(re\_emoji, text, 0, NULL);
}

/* Créer des images d'emoji dans le texte si possible */
static void emojify(GtkTextView *view, const gchar *text)
{
    if (!text || !*text) return;

    GtkTextBuffer *buf = gtk\_text\_view\_get\_buffer(view);
    GtkTextIter start, end;
    gtk\_text\_buffer\_get\_start\_iter(buf, &start);
    gtk\_text\_buffer\_get\_end\_iter(buf, &end);

    /* Supprimer les images d'emoji existantes */
    if (has\_emoji(text))
        gtk\_text\_buffer\_remove\_all\_tags\_by\_name(buf, "ai-chat-emoji");

    /* Créer des images d'emoji dans le texte */
    GMatchInfo *match\_info = NULL;
    gchar **emojis = g\_regex\_split(re\_emoji, text, 0);
    for (int i = 0; emojis[i]; ++i)
    {
        if (i % 2 == 1) /* Emoji */
        {
            GtkTextTag *tag = gtk\_text\_buffer\_create\_tag(buf, "ai-chat-emoji",
                                                             "foreground", "black",
                                                             NULL);
            GtkTextIter it;
            gtk\_text\_buffer\_get\_iter\_at\_mark(buf, &it, gtk\_text\_buffer\_get\_insert(buf));
            const gchar *emoji = emojis[i];
            if (g\_hash\_table\_contains(emoji\_hash, emoji))
            {
                GdkPixbuf *pixbuf = g\_hash\_table\_lookup(emoji\_hash, emoji);
                GtkImage *img = gtk\_image\_new\_from\_pixbuf(pixbuf);
                gtk\_text\_buffer\_insert\_interactive\_at\_cursor(buf, NULL, img, &it);
            }
            else
            {
                gchar *str = g\_strdup\_printf("<span foreground=\"black\">:%s:</span>", emoji);
                gtk\_text\_buffer\_insert(buf, &it, str, -1);
                g\_free(str);
            }
            gtk\_text\_buffer\_apply\_tag(buf, tag, &it, NULL);
        }
        else /* Texte normal */
        {
            GtkTextIter it;
            gtk\_text\_buffer\_get\_iter\_at\_mark(buf, &it, gtk\_text\_buffer\_get\_insert(buf));
            gtk\_text\_buffer\_insert(buf, &it, emojis[i], -1);
        }
    }
    g\_strfreev(emojis);
}

/* --- Autres --- */

static void add\_info\_row(const gchar *text)
{
    GtkWidget *box = gtk\_box\_new(GTK\_ORIENTATION\_HORIZONTAL, 6);
    GtkWidget *lab = gtk\_label\_new(text);
    gtk\_widget\_set\_halign(lab, GTK\_ALIGN\_START);
    GtkWidget *row = gtk\_list\_box\_row\_new();
    gtk\_container\_add(GTK\_CONTAINER(row), box);
    gtk\_container\_add(GTK\_CONTAINER(box), lab);
    gtk\_list\_box\_insert(ui.msg\_list, row, -1);
}

static void add\_message\_row(const gchar *role, const gchar *content)
{
    GtkWidget *box = gtk\_box\_new(GTK\_ORIENTATION\_HORIZONTAL, 6);
    GtkWidget *lab = gtk\_label\_new(NULL);
    gtk\_widget\_set\_halign(lab, GTK\_ALIGN\_START);
    gtk\_widget\_set\_margin\_start(lab, 12);
    gtk\_widget\_set\_margin\_end(lab, 64);
    GtkWidget *role\_lab = gtk\_label\_new(role);
    gtk\_widget\_set\_halign(role\_lab, GTK\_ALIGN\_END);
    GtkWidget *row = gtk\_list\_box\_row\_new();
    gtk\_container\_add(GTK\_CONTAINER(row), box);
    gtk\_container\_add(GTK\_CONTAINER(box), role\_lab);
    gtk\_container\_add(GTK\_CONTAINER(box), lab);
    gtk\_list\_box\_insert(ui.msg\_list, row, -1);

    GtkTextBuffer *buf = gtk\_text\_view\_get\_buffer(ui.input\_view);
    GtkTextIter it;
    gtk\_text\_buffer\_get\_end\_iter(buf, &it);
    gtk\_text\_buffer\_insert(buf, &it, content, -1);

    linkify(ui.input\_view, content);
    emojify(ui.input\_view, content);
}

/* -------------------------- Actions ------------------------------------- */

static void send\_prompt(const gchar *prompt)
{
    if (!prompt || !*prompt) return;

    ApiMode mode; gchar *base; gchar *model; gdouble temp;
    gchar *key; gboolean stream;
    read\_prefs\_from\_ui(&mode, &base, &model, &temp, &key, &stream);
    save\_prefs\_from\_vals(mode, base, model, temp, key, stream);

    add\_info\_row("Utilisateur:");
    add\_message\_row("Utilisateur", prompt);

    Req *req = g\_new0(Req, 1);
    req->prompt    = g\_strdup(prompt);
    req->mode      = mode;
    req->base      = base;
    req->model     = model;
    req->temp      = temp;
    req->api\_key   = key;
    req->streaming = stream;
    req->accum     = g\_string\_new(NULL);
    g\_atomic\_int\_set(&req->cancel, 0);

    add\_info\_row("Assistant:");
    add\_message\_row("Assistant", "...");

    current\_req = req;
    g\_idle\_add(set\_busy\_idle\_cb, GINT\_TO\_POINTER(TRUE));
    g\_thread\_new("ai\_chat\_http", net\_thread, req);
}

/* -------------------------- Lecture des préférences ---------------------- */

static void read\_prefs\_from\_ui(ApiMode *mode, gchar **base, gchar **model,
                                   gdouble *temp, gchar **key, gboolean *stream)
{
    *mode  = (ApiMode) gtk\_combo\_box\_get\_active(GTK\_COMBO\_BOX(ui.cmb\_api));
    *base  = g\_strdup(gtk\_entry\_get\_text(GTK\_ENTRY(ui.ent\_url)));
    *model = g\_strdup(gtk\_entry\_get\_text(GTK\_ENTRY(ui.ent\_model)));
    *temp  = gtk\_spin\_button\_get\_value(GTK\_SPIN\_BUTTON(ui.spin\_temp));
    *key   = g\_strdup(gtk\_entry\_get\_text(GTK\_ENTRY(ui.ent\_key)));
    *stream= gtk\_toggle\_button\_get\_active(GTK\_TOGGLE\_BUTTON(ui.chk\_stream));
}

static void save\_prefs\_from\_vals(ApiMode mode, const gchar *base, const gchar *model,
                                     gdouble temp, const gchar *key, gboolean stream)
{
    prefs.api\_mode    = mode;
    g\_free(prefs.base\_url); prefs.base\_url = g\_strdup(base);
    g\_free(prefs.model);    prefs.model    = g\_strdup(model);
    prefs.temperature = temp;
    g\_free(prefs.api\_key);  prefs.api\_key  = g\_strdup(key);
    prefs.streaming   = stream;
    prefs\_save();
}

/* -------------------------- Initialisation ------------------------------ */

static void init\_ui(void)
{
    /* Créer les expressions régulières pour la linkification et l'emojification */
    re\_url = g\_regex\_new("\\bhttps?://[^\\s]+\\b", G\_REGEX\_CASELESS, 0, NULL);
    re\_emoji = g\_regex\_new(":([a-zA-Z0-9+/]+):", G\_REGEX\_CASELESS, 0, NULL);

    /* Charger les emojis */
    emoji\_hash = load\_emojis();

    /* Créer la vue de saisie et le buffer associé */
    ui.input\_view = gtk\_text\_view\_new();
    ui.input\_buf = gtk\_text\_view\_get\_buffer(ui.input\_view);
    g\_signal\_connect(ui.input\_view, "key-press-event", G\_CALLBACK(on\_input\_key), NULL);

    /* Créer les boutons */
    ui.btn\_send = gtk\_button\_new\_with\_label("Envoyer");
    g\_signal\_connect(ui.btn\_send, "clicked", G\_CALLBACK(on\_send), NULL);

    ui.btn\_send\_sel = gtk\_button\_new\_with\_label("Envoyer sélection");
    g\_signal\_connect(ui.btn\_send\_sel, "clicked", G\_CALLBACK(on\_send\_selection), NULL);

    /* Créer la liste des messages */
    ui.msg\_list = gtk\_list\_box\_new();
    gtk\_container\_add(GTK\_CONTAINER(ui.scroll), ui.msg\_list);

    /* Ajouter les widgets à l'interface utilisateur */
    GtkWidget *content = g\_plugin->geany\_data->main\_widgets->message\_window;
    gtk\_container\_add(GTK\_CONTAINER(content), ui.root\_box);
}

/* -------------------------- Nettoyage ----------------------------------- */

static void cleanup\_ui(void)
{
    /* Détruire les expressions régulières pour la linkification et l'emojification */
    g\_regex\_unref(re\_url);
    g\_regex\_unref(re\_emoji);

    /* Libérer la mémoire des emojis */
    g\_hash\_table\_destroy(emoji\_hash);
}

/* -------------------------- Initialisation du plugin --------------------- */

static gboolean my\_plugin\_init(GeanyPlugin *plugin, gpointer data)
{
    (void)data;
    g\_plugin = plugin;
    prefs\_load();
    init\_ui();
    return TRUE;
}

static void my\_plugin\_cleanup(GeanyPlugin *plugin, gpointer data)
{
    (void)plugin; (void)data;
    cleanup\_ui();
    prefs\_save();
}

static PluginCallback callbacks[] = { { NULL, NULL, FALSE, NULL } };

G\_MODULE\_EXPORT
void geany\_load\_module(GeanyPlugin *plugin)
{
    plugin->info->name        = "AI Chat (pro)";
    plugin->info->description = "Chat local (Ollama/OpenAI) avec streaming, "
                                "sélection éditeur, Stop et code blocks colorés.";
    plugin->info->version     = "1.7";
    plugin->info->author      = "Toi";

    plugin->funcs->init      = my\_plugin\_init;
    plugin->funcs->cleanup   = my\_plugin\_cleanup;
    plugin->funcs->callbacks = callbacks;

    GEANY\_PLUGIN\_REGISTER(plugin, GEANY\_API\_VERSION);
}
