/* json.c — see json.h. */
#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- input ---------------- */
static const char *ws(const char *p)
{
    while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
        p++;
    }
    return p;
}

/* A JSON string at p (after the opening quote) into out; returns the char after the closing quote or NULL. */
static const char *str_tok(const char *p, char *out, size_t n)
{
    size_t k = 0u;
    while ((*p != '\0') && (*p != '"')) {
        char c = *p++;
        if (c == '\\') {
            const char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': /* \uXXXX: keep ASCII, replace the rest */
                c = '?';
                if ((p[0] != '\0') && (p[1] != '\0') && (p[2] != '\0') && (p[3] != '\0')) {
                    char hex[5] = {p[0], p[1], p[2], p[3], '\0'};
                    const long cp = strtol(hex, NULL, 16);
                    c = ((cp > 0) && (cp < 128)) ? (char)cp : '?';
                    p += 4;
                }
                break;
            case '\0': return NULL;
            default: c = e; break; /* \" \\ \/ */
            }
        }
        if ((k + 1u) < n) {
            out[k++] = c;
        }
    }
    out[k] = '\0';
    return (*p == '"') ? (p + 1) : NULL;
}

bool jin_parse(const char *line, jin_t *o)
{
    o->n = 0;
    const char *p = ws(line);
    if (*p++ != '{') {
        return false;
    }
    p = ws(p);
    if (*p == '}') {
        return true;
    }
    for (;;) {
        if ((*p != '"') || (o->n >= JIN_MAX_KEYS)) {
            return false;
        }
        const int i = o->n;
        p = str_tok(p + 1, o->key[i], JIN_KEY_LEN);
        if (p == NULL) {
            return false;
        }
        p = ws(p);
        if (*p++ != ':') {
            return false;
        }
        p = ws(p);
        o->is_str[i] = (*p == '"');
        if (o->is_str[i]) {
            p = str_tok(p + 1, o->val[i], JIN_VAL_LEN);
            if (p == NULL) {
                return false;
            }
        } else {
            size_t k = 0u;
            while ((*p != '\0') && (*p != ',') && (*p != '}') && (*p != ' ') && (*p != '\t')) {
                if ((*p == '{') || (*p == '[')) {
                    return false; /* flat objects only */
                }
                if ((k + 1u) < JIN_VAL_LEN) {
                    o->val[i][k++] = *p;
                }
                p++;
            }
            o->val[i][k] = '\0';
            if (k == 0u) {
                return false;
            }
        }
        o->n++;
        p = ws(p);
        if (*p == ',') {
            p = ws(p + 1);
            continue;
        }
        return *p == '}';
    }
}

static int find(const jin_t *o, const char *key)
{
    for (int i = 0; i < o->n; i++) {
        if (strcmp(o->key[i], key) == 0) {
            return i;
        }
    }
    return -1;
}

const char *jin_str(const jin_t *o, const char *key)
{
    const int i = find(o, key);
    return ((i >= 0) && o->is_str[i]) ? o->val[i] : NULL;
}

bool jin_num(const jin_t *o, const char *key, double *v)
{
    const int i = find(o, key);
    if (i < 0) {
        return false;
    }
    char *end = NULL;
    const double x = strtod(o->val[i], &end);
    if ((end == o->val[i]) || (*end != '\0') || !isfinite(x)) {
        return false;
    }
    *v = x;
    return true;
}

bool jin_bool(const jin_t *o, const char *key, bool *v)
{
    const int i = find(o, key);
    if ((i < 0) || o->is_str[i]) {
        return false;
    }
    if (strcmp(o->val[i], "true") == 0) {
        *v = true;
        return true;
    }
    if (strcmp(o->val[i], "false") == 0) {
        *v = false;
        return true;
    }
    return false;
}

/* ---------------- output ---------------- */
#define JO_CAP 131072u
static char s_buf[JO_CAP];
static size_t s_len;
static bool s_first[9];
static char s_kind[9]; /* the closing character of each open level */
static int s_depth;

static void put(const char *s, size_t n)
{
    if ((s_len + n) < JO_CAP) {
        memcpy(&s_buf[s_len], s, n);
        s_len += n;
    }
}

static void puts_(const char *s) { put(s, strlen(s)); }

static void put_str(const char *s)
{
    put("\"", 1u);
    for (; *s != '\0'; s++) {
        const unsigned char c = (unsigned char)*s;
        if ((c == '"') || (c == '\\')) {
            char e[2] = {'\\', (char)c};
            put(e, 2u);
        } else if (c < 0x20u) {
            char e[8];
            (void)snprintf(e, sizeof e, "\\u%04x", (unsigned)c);
            puts_(e);
        } else {
            put((const char *)&c, 1u);
        }
    }
    put("\"", 1u);
}

void jo_key(const char *k)
{
    if (!s_first[s_depth]) {
        put(",", 1u);
    }
    s_first[s_depth] = false;
    if (k != NULL) {
        put_str(k);
        put(":", 1u);
    }
}

void jo_begin(void)
{
    s_len = 0u;
    s_depth = 0;
    s_first[0] = true;
    put("{", 1u);
}

void jo_end(void)
{
    while (s_depth > 0) {
        jo_close();
    }
    put("}\n", 2u);
    (void)fwrite(s_buf, 1u, s_len, stdout);
    (void)fflush(stdout);
}

void jo_d(const char *k, double v)
{
    jo_key(k);
    if (!isfinite(v)) {
        puts_("null");
        return;
    }
    char t[40];
    (void)snprintf(t, sizeof t, "%.7g", v);
    puts_(t);
}

void jo_f(const char *k, float v) { jo_d(k, (double)v); }

void jo_i(const char *k, long long v)
{
    jo_key(k);
    char t[32];
    (void)snprintf(t, sizeof t, "%lld", v);
    puts_(t);
}

void jo_u(const char *k, unsigned long long v)
{
    jo_key(k);
    char t[32];
    (void)snprintf(t, sizeof t, "%llu", v);
    puts_(t);
}

void jo_b(const char *k, bool v)
{
    jo_key(k);
    puts_(v ? "true" : "false");
}

void jo_s(const char *k, const char *s)
{
    jo_key(k);
    put_str(s);
}

void jo_raw(const char *k, const char *raw)
{
    jo_key(k);
    puts_(raw);
}

static void open_(const char *k, char open, char close)
{
    if (s_depth >= 8) {
        return; /* deeper nesting is a programming error: dropped, the line stays valid JSON */
    }
    jo_key(k);
    put(&open, 1u);
    s_depth++;
    s_first[s_depth] = true;
    s_kind[s_depth] = close;
}

void jo_obj(const char *k) { open_(k, '{', '}'); }
void jo_arr(const char *k) { open_(k, '[', ']'); }

void jo_close(void)
{
    if (s_depth > 0) {
        put(&s_kind[s_depth], 1u);
        s_depth--;
    }
}
