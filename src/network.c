/*
 * network.c — HTTP/curl networking for AI Chat plugin
 */

#include "network.h"
#include "history.h"
#include "prefs.h"
#include <curl/curl.h>
#include <string.h>

Req *current_req = NULL;

/* Callbacks set by UI module */
static StreamAppendFunc g_stream_append = NULL;
static ReplaceRowFunc   g_replace_row   = NULL;
static SetBusyFunc      g_set_busy      = NULL;

void network_set_callbacks(StreamAppendFunc stream_append,
                           ReplaceRowFunc replace_row,
                           SetBusyFunc set_busy)
{
    g_stream_append = stream_append;
    g_replace_row   = replace_row;
    g_set_busy      = set_busy;
}

void network_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void network_cleanup(void)
{
    curl_global_cleanup();
}

/* --- Memory collection callback ------------------------------------------ */

struct Mem { char *data; size_t size; };

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

/* --- JSON string unescape (UTF-8) ---------------------------------------- */

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
                    gstring_append_utf8_cp(dst, 0xFFFD);
                }
                else
                {
                    gstring_append_utf8_cp(dst, u);
                }
                break;
            }
            default:
                g_string_append_c(dst, *p++);
                break;
        }
    }
}

/* --- Stream append via UI callback --------------------------------------- */

static void append_decoded_segment(Req *req, const char *start, const char *end)
{
    GString *dec = g_string_new(NULL);
    json_unescape_append(dec, start, end);
    if (dec->len)
    {
        if (!req->accum) req->accum = g_string_new(NULL);
        g_string_append_len(req->accum, dec->str, dec->len);
        if (g_stream_append)
            g_stream_append(req, dec->str, (gssize)dec->len);
    }
    g_string_free(dec, TRUE);
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

/* --- Streaming callbacks ------------------------------------------------- */

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

/* --- Network thread ------------------------------------------------------ */

static gpointer net_thread(gpointer data)
{
    Req *req = (Req *)data;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        if (g_stream_append)
            g_stream_append(req, "[Erreur] curl init\n", -1);
        goto done;
    }

    gchar *url = NULL;
    gchar *payload = NULL;
    struct curl_slist *hdr = NULL;

    if (req->mode == API_OLLAMA)
    {
        url = g_strdup_printf("%s/api/chat", req->base);
        history_add("user", req->prompt);
        {
            GString *gs = g_string_new(NULL);
            g_string_append(gs, "{\"model\":\"");
            g_string_append(gs, req->model);
            g_string_append(gs, "\",\"messages\":");
            g_string_append(gs, history_get_json());
            g_string_append(gs, ",\"stream\":");
            g_string_append(gs, req->streaming ? "true" : "false");
            g_string_append(gs, ",\"options\":{");
            json_append_double(gs, "temperature", req->temp);
            g_string_append(gs, "}}");
            payload = g_string_free(gs, FALSE);
        }
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
    }
    else
    {
        url = g_strdup_printf("%s/v1/chat/completions", req->base);
        gchar *esc_user = json_escape(req->prompt);
        gchar *esc_sys = NULL;
        gboolean has_sys = (prefs.system_prompt && *prefs.system_prompt);
        if (has_sys) esc_sys = json_escape(prefs.system_prompt);
        {
            GString *gs = g_string_new(NULL);
            g_string_append(gs, "{\"model\":\"");
            g_string_append(gs, req->model);
            g_string_append(gs, "\",\"messages\":[");
            if (has_sys)
            {
                g_string_append(gs, "{\"role\":\"system\",\"content\":\"");
                g_string_append(gs, esc_sys);
                g_string_append(gs, "\"},");
            }
            g_string_append(gs, "{\"role\":\"user\",\"content\":\"");
            g_string_append(gs, esc_user);
            g_string_append(gs, "\"}]");
            g_string_append(gs, ",\"temperature\":");
            json_append_double(gs, NULL, req->temp);
            g_string_append(gs, ",\"stream\":");
            g_string_append(gs, req->streaming ? "true" : "false");
            g_string_append(gs, "}");
            payload = g_string_free(gs, FALSE);
        }
        g_free(esc_user);
        g_free(esc_sys);
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

    /* Apply timeout (0 = no limit) */
    if (prefs.timeout > 0)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)prefs.timeout);

    /* Apply proxy if configured */
    if (prefs.proxy && *prefs.proxy)
        curl_easy_setopt(curl, CURLOPT_PROXY, prefs.proxy);

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
        {
            if (g_stream_append)
                g_stream_append(req, "\n[Annulé]\n", -1);
        }
        else if (rc != CURLE_OK)
        {
            long code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            gchar *msg = g_strdup_printf("\n[Erreur] streaming: %s (HTTP %ld)\n", curl_easy_strerror(rc), code);
            if (g_stream_append)
                g_stream_append(req, msg, -1);
            g_free(msg);
        }
    }
    else
    {
        struct Mem mem = {0};
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_ABORTED_BY_CALLBACK)
        {
            if (g_stream_append)
                g_stream_append(req, "\n[Annulé]\n", -1);
        }
        else if (rc != CURLE_OK)
        {
            long code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            gchar *msg = g_strdup_printf("\n[Erreur] requête: %s (HTTP %ld)\n", curl_easy_strerror(rc), code);
            if (g_stream_append)
                g_stream_append(req, msg, -1);
            g_free(msg);
        }
        else if (mem.data && mem.size)
        {
            long code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            if (code >= 300)
            {
                gchar *msg = g_strdup_printf("\n[Erreur] HTTP %ld\n", code);
                if (g_stream_append)
                    g_stream_append(req, msg, -1);
                g_free(msg);
            }
            extract_content_and_append(mem.data, mem.size, req);
        }
        g_free(mem.data);
    }

    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    g_free(url);
    g_free(payload);

done:
    {
        gchar *final = req->accum ? g_string_free(req->accum, FALSE) : g_strdup("");
        req->accum = NULL;

        if (final && *final)
            history_add("assistant", final);

        if (g_replace_row)
            g_replace_row(req->row, final);
        else
            g_free(final);
    }

    if (g_set_busy)
        g_set_busy(FALSE);

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

void network_send_request(Req *req)
{
    current_req = req;
    if (g_set_busy)
        g_set_busy(TRUE);
    g_thread_new("ai_chat_http", net_thread, req);
}
