/*
 * models.c — Model list fetching for AI Chat plugin
 */

#include "models.h"
#include <curl/curl.h>
#include <string.h>

/* --- Request context ----------------------------------------------------- */

typedef struct {
    ApiMode mode;
    gchar *base_url;
    gchar *api_key;
    ModelsFetchedCallback callback;
    gpointer user_data;
} FetchCtx;

/* --- Memory buffer for curl ---------------------------------------------- */

struct MemBuf {
    char *data;
    size_t size;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    size_t realsize = size * nmemb;
    struct MemBuf *mem = (struct MemBuf *)ud;
    char *p = g_realloc(mem->data, mem->size + realsize + 1);
    if (!p) return 0;
    mem->data = p;
    memcpy(mem->data + mem->size, ptr, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

/* --- Simple JSON parsing for model names --------------------------------- */

/*
 * Parse Ollama response: {"models": [{"name": "llama3:8b", ...}, ...]}
 * Returns GList of model names (gchar*)
 */
static GList* parse_ollama_models(const char *json)
{
    GList *list = NULL;
    if (!json) return NULL;

    /* Find "models" array */
    const char *models = strstr(json, "\"models\"");
    if (!models) return NULL;

    const char *arr_start = strchr(models, '[');
    if (!arr_start) return NULL;

    const char *p = arr_start + 1;
    while (*p)
    {
        /* Find "name" field */
        const char *name_key = strstr(p, "\"name\"");
        if (!name_key) break;

        const char *colon = strchr(name_key + 6, ':');
        if (!colon) break;

        /* Skip whitespace and find opening quote */
        const char *q = colon + 1;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '"') { p = q; continue; }

        q++; /* skip opening quote */
        const char *end = strchr(q, '"');
        if (!end) break;

        gchar *name = g_strndup(q, end - q);
        list = g_list_append(list, name);

        p = end + 1;

        /* Check for end of array or next object */
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ']') break;
        if (*p == ',') p++;
    }

    return list;
}

/*
 * Parse OpenAI response: {"data": [{"id": "gpt-4", ...}, ...]}
 * Returns GList of model IDs (gchar*)
 */
static GList* parse_openai_models(const char *json)
{
    GList *list = NULL;
    if (!json) return NULL;

    /* Find "data" array */
    const char *data = strstr(json, "\"data\"");
    if (!data) return NULL;

    const char *arr_start = strchr(data, '[');
    if (!arr_start) return NULL;

    const char *p = arr_start + 1;
    while (*p)
    {
        /* Find "id" field */
        const char *id_key = strstr(p, "\"id\"");
        if (!id_key) break;

        const char *colon = strchr(id_key + 4, ':');
        if (!colon) break;

        const char *q = colon + 1;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '"') { p = q; continue; }

        q++;
        const char *end = strchr(q, '"');
        if (!end) break;

        gchar *id = g_strndup(q, end - q);
        list = g_list_append(list, id);

        p = end + 1;

        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ']') break;
        if (*p == ',') p++;
    }

    return list;
}

/* --- Idle callback to deliver results on main thread --------------------- */

typedef struct {
    GList *models;
    ModelsFetchedCallback callback;
    gpointer user_data;
} DeliverCtx;

static gboolean deliver_idle_cb(gpointer data)
{
    DeliverCtx *ctx = (DeliverCtx *)data;
    if (ctx->callback)
        ctx->callback(ctx->models, ctx->user_data);
    g_free(ctx);
    return FALSE;
}

/* --- Fetch thread -------------------------------------------------------- */

static gpointer fetch_thread(gpointer data)
{
    FetchCtx *ctx = (FetchCtx *)data;
    GList *models = NULL;

    CURL *curl = curl_easy_init();
    if (!curl)
        goto done;

    gchar *url = NULL;
    struct curl_slist *headers = NULL;
    struct MemBuf mem = {0};

    if (ctx->mode == API_OLLAMA)
    {
        url = g_strdup_printf("%s/api/tags", ctx->base_url);
    }
    else
    {
        url = g_strdup_printf("%s/v1/models", ctx->base_url);
        if (ctx->api_key && *ctx->api_key)
        {
            gchar *auth = g_strdup_printf("Authorization: Bearer %s", ctx->api_key);
            headers = curl_slist_append(headers, auth);
            g_free(auth);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK && mem.data)
    {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code >= 200 && http_code < 300)
        {
            if (ctx->mode == API_OLLAMA)
                models = parse_ollama_models(mem.data);
            else
                models = parse_openai_models(mem.data);
        }
    }

    g_free(mem.data);
    g_free(url);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

done:
    /* Deliver results on main thread */
    {
        DeliverCtx *dctx = g_new0(DeliverCtx, 1);
        dctx->models = models;
        dctx->callback = ctx->callback;
        dctx->user_data = ctx->user_data;
        g_idle_add(deliver_idle_cb, dctx);
    }

    g_free(ctx->base_url);
    g_free(ctx->api_key);
    g_free(ctx);

    return NULL;
}

/* --- Public API ---------------------------------------------------------- */

void models_fetch_async(ApiMode mode,
                        const gchar *base_url,
                        const gchar *api_key,
                        ModelsFetchedCallback callback,
                        gpointer user_data)
{
    FetchCtx *ctx = g_new0(FetchCtx, 1);
    ctx->mode = mode;
    ctx->base_url = g_strdup(base_url);
    ctx->api_key = g_strdup(api_key ? api_key : "");
    ctx->callback = callback;
    ctx->user_data = user_data;

    g_thread_new("models_fetch", fetch_thread, ctx);
}
