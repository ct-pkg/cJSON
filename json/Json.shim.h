/*
 * Json.shim.h - the C side of the json package.
 *
 * Two layers live here:
 *
 *   1. the strict validator: a single left-to-right pass over the text that
 *      validates RFC 8259 and produces every `[strict]` error with the exact
 *      byte offset. It runs before cJSON so that rejected input allocates no
 *      tree, and so that error positions do not depend on cJSON, whose `end`
 *      pointer is far too coarse (an unterminated string reports offset 1).
 *
 *   2. the thin wrappers cJSON needs: the doc, the accessors, the builders and
 *      the printer. Everything is `static inline`, so the unit emits nothing
 *      and the consumer gets exactly what it uses.
 *
 * Nothing here is public CT surface: the only names the CT side sees are the
 * `ct_json_*` wrappers referenced from Json.m.ct.
 */
#ifndef CT_JSON_SHIM_H
#define CT_JSON_SHIM_H

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

/* Mirror of JsonStatus in JsonTypes.h.ct. The CT_ prefix keeps these clear of
 * the enum constants the consumer also gets from JsonTypes.h.ct. */
#define CT_JSON_OK              0
#define CT_JSON_ERR_SYNTAX     -1
#define CT_JSON_ERR_DUP_KEY    -2
#define CT_JSON_ERR_NONFINITE  -3
#define CT_JSON_ERR_LIMIT      -4
#define CT_JSON_ERR_UTF8       -5
#define CT_JSON_ERR_CONTROL    -6
#define CT_JSON_ERR_BOM        -7

/* Depth cannot exceed cJSON's compile-time limit (ct.pkg passes
 * -DCJSON_NESTING_LIMIT=64), so the runtime knob only tightens it. */
#define CT_JSON_DEPTH_CAP          CJSON_NESTING_LIMIT
#define CT_JSON_DEFAULT_MAX_DEPTH  64
#define CT_JSON_DEFAULT_MAX_NODES  100000
#define CT_JSON_DEFAULT_MAX_STRING 1048576
#define CT_JSON_DEFAULT_MAX_INPUT  8388608

typedef struct ct_json_doc ct_json_doc;

struct ct_json_doc {
    cJSON *root;
    char *out;          /* last rendered text, borrowed by the caller */
    int err;
    int offset;         /* 0-based byte offset, -1 when clean */
    int line;           /* 1-based */
    int col;            /* 1-based, by byte */
    int max_depth;
    int max_nodes;
    int max_string;
    int max_input;
};

/* ------------------------------------------------------------------ */
/* strict validator                                                    */
/* ------------------------------------------------------------------ */

typedef struct ct_json_keyref {
    const char *p;      /* points into the text when off < 0 */
    int off;            /* >= 0: offset inside the arena */
    int len;            /* decoded length in bytes */
} ct_json_keyref;

typedef struct ct_json_scan {
    const char *text;
    int len;
    int i;
    int nodes;
    int max_depth;
    int max_nodes;
    int max_string;
    int err;
    int at;

    char *arena;        /* decoded bytes of keys that contain escapes */
    int arena_len;
    int arena_cap;

    ct_json_keyref *keys;
    int keys_n;
    int keys_cap;

    int level_first[CT_JSON_DEPTH_CAP + 1];
} ct_json_scan;

static inline void ct_json_scan_fail(ct_json_scan *s, int code, int at)
{
    if (s->err == 0) {
        s->err = code;
        s->at = at;
    }
}

static inline int ct_json_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline int ct_json_is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static inline int ct_json_hex4(const char *p, int avail)
{
    int v = 0;
    int k;
    if (avail < 4) {
        return -1;
    }
    for (k = 0; k < 4; k++) {
        unsigned char c = (unsigned char)p[k];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v |= c - 'A' + 10;
        } else {
            return -1;
        }
    }
    return v;
}

/* Length in bytes of a UTF-8 encoding for `cp`, without encoding it. */
static inline int ct_json_utf8_width(int cp)
{
    if (cp < 0x80) {
        return 1;
    }
    if (cp < 0x800) {
        return 2;
    }
    if (cp < 0x10000) {
        return 3;
    }
    return 4;
}

/* Validates one UTF-8 sequence at p (avail bytes). Returns 1..4, or 0 when the
 * sequence is malformed (overlong, surrogate, > U+10FFFF, truncated). */
static inline int ct_json_utf8_len(const char *p, int avail)
{
    unsigned char c = (unsigned char)p[0];
    unsigned char c1;
    unsigned char c2;
    unsigned char c3;

    if (c < 0x80) {
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        if (avail < 2) {
            return 0;
        }
        c1 = (unsigned char)p[1];
        return (c1 >= 0x80 && c1 <= 0xBF) ? 2 : 0;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        if (avail < 3) {
            return 0;
        }
        c1 = (unsigned char)p[1];
        c2 = (unsigned char)p[2];
        if (c2 < 0x80 || c2 > 0xBF) {
            return 0;
        }
        if (c == 0xE0) {
            return (c1 >= 0xA0 && c1 <= 0xBF) ? 3 : 0;
        }
        if (c == 0xED) {
            return (c1 >= 0x80 && c1 <= 0x9F) ? 3 : 0;
        }
        return (c1 >= 0x80 && c1 <= 0xBF) ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (avail < 4) {
            return 0;
        }
        c1 = (unsigned char)p[1];
        c2 = (unsigned char)p[2];
        c3 = (unsigned char)p[3];
        if (c2 < 0x80 || c2 > 0xBF || c3 < 0x80 || c3 > 0xBF) {
            return 0;
        }
        if (c == 0xF0) {
            return (c1 >= 0x90 && c1 <= 0xBF) ? 4 : 0;
        }
        if (c == 0xF4) {
            return (c1 >= 0x80 && c1 <= 0x8F) ? 4 : 0;
        }
        return (c1 >= 0x80 && c1 <= 0xBF) ? 4 : 0;
    }
    return 0;
}

static inline void ct_json_put_utf8(char *out, int cp)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
    }
}

/*
 * Scans one string literal. On entry s->i points at the opening quote. On
 * success s->i points just past the closing quote and *decoded receives the
 * decoded byte length (used for max_string), *has_escape tells whether the raw
 * span contains a backslash.
 */
static inline int ct_json_scan_string(ct_json_scan *s, int start, int *decoded, int *has_escape)
{
    int n = 0;
    int esc = 0;

    s->i = start + 1;
    for (;;) {
        unsigned char c;
        int unit_at = s->i;
        if (s->i >= s->len) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, start);
            return -1;
        }
        c = (unsigned char)s->text[s->i];
        if (c == '"') {
            s->i++;
            break;
        }
        if (c == '\\') {
            int esc_at = s->i;
            int avail;
            unsigned char e;
            esc = 1;
            s->i++;
            if (s->i >= s->len) {
                ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                return -1;
            }
            e = (unsigned char)s->text[s->i];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't') {
                n += 1;
                s->i++;
            } else if (e == 'u') {
                int cp;
                avail = s->len - (s->i + 1);
                cp = ct_json_hex4(s->text + s->i + 1, avail);
                if (cp < 0) {
                    ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                    return -1;
                }
                s->i += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    int low;
                    avail = s->len - s->i;
                    if (avail < 6 || s->text[s->i] != '\\' || s->text[s->i + 1] != 'u') {
                        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                        return -1;
                    }
                    low = ct_json_hex4(s->text + s->i + 2, avail - 2);
                    if (low < 0xDC00 || low > 0xDFFF) {
                        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                        return -1;
                    }
                    s->i += 6;
                    n += 4;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                    return -1;
                } else if (cp == 0) {
                    /* RFC 8259 allows \u0000, but a NUL inside a value cannot
                     * survive the char* surface; reject it explicitly instead
                     * of silently truncating. */
                    ct_json_scan_fail(s, CT_JSON_ERR_CONTROL, esc_at);
                    return -1;
                } else {
                    n += ct_json_utf8_width(cp);
                }
            } else {
                ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, esc_at);
                return -1;
            }
        } else if (c < 0x20) {
            ct_json_scan_fail(s, CT_JSON_ERR_CONTROL, s->i);
            return -1;
        } else if (c < 0x80) {
            n += 1;
            s->i++;
        } else {
            int k = ct_json_utf8_len(s->text + s->i, s->len - s->i);
            if (k == 0) {
                ct_json_scan_fail(s, CT_JSON_ERR_UTF8, s->i);
                return -1;
            }
            n += k;
            s->i += k;
        }
        if (n > s->max_string) {
            /* Report the byte (or the backslash of the escape) that pushed the
             * decoded length over the limit. */
            ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, unit_at);
            return -1;
        }
    }
    *decoded = n;
    *has_escape = esc;
    return 0;
}

/* Decodes an already validated string body (raw excludes the quotes) into out.
 * Returns the number of bytes written. */
static inline int ct_json_decode_string(const char *raw, int raw_len, char *out)
{
    int i = 0;
    int n = 0;

    while (i < raw_len) {
        unsigned char c = (unsigned char)raw[i];
        if (c != '\\') {
            out[n++] = raw[i++];
            continue;
        }
        i++;
        switch (raw[i]) {
        case '"':  out[n++] = '"';  i++; break;
        case '\\': out[n++] = '\\'; i++; break;
        case '/':  out[n++] = '/';  i++; break;
        case 'b':  out[n++] = '\b'; i++; break;
        case 'f':  out[n++] = '\f'; i++; break;
        case 'n':  out[n++] = '\n'; i++; break;
        case 'r':  out[n++] = '\r'; i++; break;
        case 't':  out[n++] = '\t'; i++; break;
        case 'u': {
            int cp = ct_json_hex4(raw + i + 1, raw_len - (i + 1));
            i += 5;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                int low = ct_json_hex4(raw + i + 2, raw_len - (i + 2));
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 6;
            }
            ct_json_put_utf8(out + n, cp);
            n += ct_json_utf8_width(cp);
            break;
        }
        default:
            /* unreachable: the scanner rejected every other escape */
            out[n++] = raw[i++];
            break;
        }
    }
    return n;
}

static inline char *ct_json_arena_take(ct_json_scan *s, int n)
{
    if (s->arena_len + n > s->arena_cap) {
        int cap = s->arena_cap ? s->arena_cap : 256;
        char *grown;
        while (cap < s->arena_len + n) {
            cap *= 2;
        }
        grown = (char *)realloc(s->arena, (size_t)cap);
        if (!grown) {
            return 0;
        }
        s->arena = grown;
        s->arena_cap = cap;
    }
    return s->arena + s->arena_len;
}

static inline int ct_json_keyref_same(ct_json_scan *s, const ct_json_keyref *k, const char *p, int len)
{
    const char *q = (k->off >= 0) ? (s->arena + k->off) : k->p;
    if (k->len != len) {
        return 0;
    }
    return len == 0 || memcmp(q, p, (size_t)len) == 0;
}

/*
 * Reads one object member name, rejects a decoded duplicate, and records the
 * key for the current object level. s->i lands just past the closing quote.
 */
static inline int ct_json_scan_key(ct_json_scan *s, int level)
{
    int start = s->i;
    int decoded = 0;
    int has_escape = 0;
    int k;
    ct_json_keyref ref;

    if (ct_json_scan_string(s, start, &decoded, &has_escape) != 0) {
        return -1;
    }

    if (has_escape) {
        int body = s->i - start - 2;
        char *dst = ct_json_arena_take(s, decoded + 1);
        if (!dst) {
            ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, start);
            return -1;
        }
        ct_json_decode_string(s->text + start + 1, body, dst);
        dst[decoded] = 0;
        ref.p = 0;
        ref.off = s->arena_len;
        ref.len = decoded;
        s->arena_len += decoded + 1;
    } else {
        ref.p = s->text + start + 1;
        ref.off = -1;
        ref.len = decoded;
    }

    for (k = s->level_first[level]; k < s->keys_n; k++) {
        if (ct_json_keyref_same(s, &s->keys[k], (ref.off >= 0) ? (s->arena + ref.off) : ref.p, ref.len)) {
            ct_json_scan_fail(s, CT_JSON_ERR_DUP_KEY, start);
            return -1;
        }
    }

    if (s->keys_n == s->keys_cap) {
        int cap = s->keys_cap ? s->keys_cap * 2 : 32;
        ct_json_keyref *grown = (ct_json_keyref *)realloc(s->keys, (size_t)cap * sizeof(*grown));
        if (!grown) {
            ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, start);
            return -1;
        }
        s->keys = grown;
        s->keys_cap = cap;
    }
    s->keys[s->keys_n++] = ref;
    return 0;
}

static inline int ct_json_scan_number(ct_json_scan *s)
{
    int start = s->i;
    int p = s->i;
    char stack_buf[64];
    char *buf = stack_buf;
    double v;

    if (p < s->len && s->text[p] == '-') {
        p++;
    }
    if (p < s->len && s->text[p] == '0') {
        p++;
        if (p < s->len && ct_json_is_digit((unsigned char)s->text[p])) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, p);
            return -1;
        }
    } else if (p < s->len && s->text[p] >= '1' && s->text[p] <= '9') {
        while (p < s->len && ct_json_is_digit((unsigned char)s->text[p])) {
            p++;
        }
    } else {
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, p);
        return -1;
    }

    if (p < s->len && s->text[p] == '.') {
        p++;
        if (p >= s->len || !ct_json_is_digit((unsigned char)s->text[p])) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, p);
            return -1;
        }
        while (p < s->len && ct_json_is_digit((unsigned char)s->text[p])) {
            p++;
        }
    }

    if (p < s->len && (s->text[p] == 'e' || s->text[p] == 'E')) {
        p++;
        if (p < s->len && (s->text[p] == '+' || s->text[p] == '-')) {
            p++;
        }
        if (p >= s->len || !ct_json_is_digit((unsigned char)s->text[p])) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, p);
            return -1;
        }
        while (p < s->len && ct_json_is_digit((unsigned char)s->text[p])) {
            p++;
        }
    }

    {
        int n = p - start;
        if (n >= (int)sizeof(stack_buf)) {
            buf = (char *)malloc((size_t)n + 1);
            if (!buf) {
                ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, start);
                return -1;
            }
        }
        memcpy(buf, s->text + start, (size_t)n);
        buf[n] = 0;
        v = strtod(buf, 0);
        if (buf != stack_buf) {
            free(buf);
        }
    }

    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
        ct_json_scan_fail(s, CT_JSON_ERR_NONFINITE, start);
        return -1;
    }
    /* strtod yields inf for overflow, and inf compares greater than DBL_MAX,
     * so the branch above catches 1e999 without pulling in <math.h>. */
    s->i = p;
    return 0;
}

static inline int ct_json_scan_value(ct_json_scan *s, int depth);

static inline int ct_json_scan_array(ct_json_scan *s, int depth)
{
    s->i++;
    while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
        s->i++;
    }
    /* `[]` is the only way to close without a value; after a comma the loop
     * head requires a value, so `[1,]` is rejected. */
    if (s->i < s->len && s->text[s->i] == ']') {
        s->i++;
        return 0;
    }
    for (;;) {
        if (ct_json_scan_value(s, depth + 1) != 0) {
            return -1;
        }
        while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
            s->i++;
        }
        if (s->i >= s->len) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->len);
            return -1;
        }
        if (s->text[s->i] == ',') {
            s->i++;
            while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
                s->i++;
            }
            continue;
        }
        if (s->text[s->i] == ']') {
            s->i++;
            return 0;
        }
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
        return -1;
    }
}

static inline int ct_json_scan_object(ct_json_scan *s, int depth)
{
    int level = depth;
    s->i++;
    s->level_first[level] = s->keys_n;
    while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
        s->i++;
    }
    /* Same shape as scan_array: `{}` is the only empty form, and the loop head
     * requires a key, so `{"a":1,}` is rejected. */
    if (s->i < s->len && s->text[s->i] == '}') {
        s->i++;
        return 0;
    }
    for (;;) {
        if (s->i >= s->len) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->len);
            return -1;
        }
        if (s->text[s->i] != '"') {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
            return -1;
        }
        if (ct_json_scan_key(s, level) != 0) {
            return -1;
        }
        while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
            s->i++;
        }
        if (s->i >= s->len) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->len);
            return -1;
        }
        if (s->text[s->i] != ':') {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
            return -1;
        }
        s->i++;
        if (ct_json_scan_value(s, depth + 1) != 0) {
            return -1;
        }
        while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
            s->i++;
        }
        if (s->i >= s->len) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->len);
            return -1;
        }
        if (s->text[s->i] == ',') {
            s->i++;
            while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
                s->i++;
            }
            continue;
        }
        if (s->text[s->i] == '}') {
            s->i++;
            return 0;
        }
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
        return -1;
    }
}

static inline int ct_json_scan_literal(ct_json_scan *s, const char *word, int n)
{
    int k;
    if (s->len - s->i < n) {
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
        return -1;
    }
    for (k = 0; k < n; k++) {
        if (s->text[s->i + k] != word[k]) {
            ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i + k);
            return -1;
        }
    }
    s->i += n;
    return 0;
}

static inline int ct_json_scan_value(ct_json_scan *s, int depth)
{
    while (s->i < s->len && ct_json_is_space((unsigned char)s->text[s->i])) {
        s->i++;
    }
    if (s->i >= s->len) {
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->len);
        return -1;
    }

    s->nodes++;
    if (s->nodes > s->max_nodes) {
        ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, s->i);
        return -1;
    }

    switch (s->text[s->i]) {
    case '{':
        if (depth > s->max_depth) {
            ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, s->i);
            return -1;
        }
        return ct_json_scan_object(s, depth);
    case '[':
        if (depth > s->max_depth) {
            ct_json_scan_fail(s, CT_JSON_ERR_LIMIT, s->i);
            return -1;
        }
        return ct_json_scan_array(s, depth);
    case '"': {
        int decoded = 0;
        int has_escape = 0;
        return ct_json_scan_string(s, s->i, &decoded, &has_escape);
    }
    case 't':
        return ct_json_scan_literal(s, "true", 4);
    case 'f':
        return ct_json_scan_literal(s, "false", 5);
    case 'n':
        return ct_json_scan_literal(s, "null", 4);
    default:
        if (s->text[s->i] == '-' || ct_json_is_digit((unsigned char)s->text[s->i])) {
            return ct_json_scan_number(s);
        }
        ct_json_scan_fail(s, CT_JSON_ERR_SYNTAX, s->i);
        return -1;
    }
}

/*
 * Validates the whole text. Returns CT_JSON_OK or an error code, with *at set
 * to the 0-based offending byte. The scan is a single pass; no tree is built.
 */
static inline int ct_json_validate(const char *text, int len, int max_depth, int max_nodes, int max_string, int *at)
{
    ct_json_scan s;

    memset(&s, 0, sizeof(s));
    s.text = text;
    s.len = len;
    s.max_depth = max_depth > CT_JSON_DEPTH_CAP ? CT_JSON_DEPTH_CAP : max_depth;
    s.max_nodes = max_nodes;
    s.max_string = max_string;
    s.err = 0;
    s.at = 0;

    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
            && (unsigned char)text[2] == 0xBF) {
        s.err = CT_JSON_ERR_BOM;
        s.at = 0;
    } else {
        if (ct_json_scan_value(&s, 1) == 0) {
            while (s.i < s.len && ct_json_is_space((unsigned char)s.text[s.i])) {
                s.i++;
            }
            if (s.i != s.len) {
                ct_json_scan_fail(&s, CT_JSON_ERR_SYNTAX, s.i);
            }
        }
    }

    free(s.keys);
    free(s.arena);
    *at = s.at;
    return s.err;
}

/* ------------------------------------------------------------------ */
/* doc                                                                 */
/* ------------------------------------------------------------------ */

static inline ct_json_doc *ct_json_doc_new(void)
{
    ct_json_doc *d = (ct_json_doc *)malloc(sizeof(*d));
    if (!d) {
        return 0;
    }
    memset(d, 0, sizeof(*d));
    d->err = 0;
    d->offset = -1;
    d->max_depth = CT_JSON_DEFAULT_MAX_DEPTH;
    d->max_nodes = CT_JSON_DEFAULT_MAX_NODES;
    d->max_string = CT_JSON_DEFAULT_MAX_STRING;
    d->max_input = CT_JSON_DEFAULT_MAX_INPUT;
    return d;
}

static inline void ct_json_doc_free(ct_json_doc *d)
{
    if (!d) {
        return;
    }
    if (d->out) {
        cJSON_free(d->out);
    }
    cJSON_Delete(d->root);
    free(d);
}

static inline void ct_json_doc_position(ct_json_doc *d, const char *text, int at)
{
    int k;
    int line = 1;
    int col = 1;
    for (k = 0; k < at; k++) {
        if (text[k] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    d->offset = at;
    d->line = line;
    d->col = col;
}

static inline int ct_json_doc_parse(ct_json_doc *d, const char *text)
{
    int at = 0;
    int err;
    int len;
    const char *end = 0;
    cJSON *root;

    if (!d) {
        return CT_JSON_ERR_SYNTAX;
    }

    if (d->out) {
        cJSON_free(d->out);
        d->out = 0;
    }
    cJSON_Delete(d->root);
    d->root = 0;
    d->err = 0;
    d->offset = -1;
    d->line = 0;
    d->col = 0;

    if (!text) {
        d->err = CT_JSON_ERR_SYNTAX;
        d->offset = 0;
        d->line = 1;
        d->col = 1;
        return d->err;
    }

    len = (int)strlen(text);
    if (len > d->max_input) {
        err = CT_JSON_ERR_LIMIT;
        at = 0;
    } else {
        err = ct_json_validate(text, len, d->max_depth, d->max_nodes, d->max_string, &at);
    }

    if (err == 0) {
        root = cJSON_ParseWithOpts(text, &end, 1);
        if (!root) {
            err = CT_JSON_ERR_SYNTAX;
            at = (end && end >= text) ? (int)(end - text) : 0;
        } else {
            d->root = root;
        }
    }

    if (err != 0) {
        d->err = err;
        ct_json_doc_position(d, text, at);
    }
    return err;
}

static inline ct_json_doc *ct_json_doc_set_root(ct_json_doc *d, cJSON *root)
{
    if (!d) {
        return 0;
    }
    if (d->out) {
        cJSON_free(d->out);
        d->out = 0;
    }
    cJSON_Delete(d->root);
    d->root = root;
    d->err = 0;
    d->offset = -1;
    d->line = 0;
    d->col = 0;
    return d;
}

static inline cJSON *ct_json_doc_root(ct_json_doc *d)
{
    return d ? d->root : 0;
}

static inline ct_json_doc *ct_json_doc_set_max_depth(ct_json_doc *d, int n)
{
    if (d && n > 0) {
        d->max_depth = n > CT_JSON_DEPTH_CAP ? CT_JSON_DEPTH_CAP : n;
    }
    return d;
}

static inline ct_json_doc *ct_json_doc_set_max_nodes(ct_json_doc *d, int n)
{
    if (d && n > 0) {
        d->max_nodes = n;
    }
    return d;
}

static inline ct_json_doc *ct_json_doc_set_max_string(ct_json_doc *d, int n)
{
    if (d && n > 0) {
        d->max_string = n;
    }
    return d;
}

static inline ct_json_doc *ct_json_doc_set_max_input(ct_json_doc *d, int n)
{
    if (d && n > 0) {
        d->max_input = n;
    }
    return d;
}

static inline char *ct_json_doc_render(ct_json_doc *d, int format)
{
    char *s;
    if (!d || !d->root) {
        return 0;
    }
    s = format ? cJSON_Print(d->root) : cJSON_PrintUnformatted(d->root);
    if (!s) {
        return 0;
    }
    if (d->out) {
        cJSON_free(d->out);
    }
    d->out = s;
    return s;
}

static inline char *ct_json_doc_write(ct_json_doc *d, int format)
{
    return ct_json_doc_render(d, format);
}

static inline int ct_json_doc_write_buf(ct_json_doc *d, char *out, int cap, int format)
{
    char *s = ct_json_doc_render(d, format);
    int n;
    int k;

    if (!s) {
        return -1;
    }
    n = (int)strlen(s);
    if (out && cap > 0) {
        k = n < cap - 1 ? n : cap - 1;
        if (k > 0) {
            memcpy(out, s, (size_t)k);
        }
        out[k] = 0;
    }
    return n;
}

static inline int ct_json_doc_error(ct_json_doc *d)
{
    return d ? d->err : CT_JSON_ERR_SYNTAX;
}

static inline int ct_json_doc_error_offset(ct_json_doc *d)
{
    return d ? d->offset : -1;
}

static inline int ct_json_doc_error_line(ct_json_doc *d)
{
    return d ? d->line : 0;
}

static inline int ct_json_doc_error_col(ct_json_doc *d)
{
    return d ? d->col : 0;
}

/* ------------------------------------------------------------------ */
/* nodes                                                               */
/* ------------------------------------------------------------------ */

static inline int ct_json_node_kind(cJSON *n)
{
    return n ? (n->type & 0xFF) : 0;
}

static inline int ct_json_node_is_null(cJSON *n)
{
    return cJSON_IsNull(n) ? 1 : 0;
}

static inline int ct_json_node_is_bool(cJSON *n)
{
    return (cJSON_IsTrue(n) || cJSON_IsFalse(n)) ? 1 : 0;
}

static inline int ct_json_node_is_number(cJSON *n)
{
    return cJSON_IsNumber(n) ? 1 : 0;
}

static inline int ct_json_node_is_string(cJSON *n)
{
    return cJSON_IsString(n) ? 1 : 0;
}

static inline int ct_json_node_is_array(cJSON *n)
{
    return cJSON_IsArray(n) ? 1 : 0;
}

static inline int ct_json_node_is_object(cJSON *n)
{
    return cJSON_IsObject(n) ? 1 : 0;
}

static inline int ct_json_node_bool(cJSON *n)
{
    return cJSON_IsTrue(n) ? 1 : 0;
}

static inline double ct_json_node_number(cJSON *n)
{
    return cJSON_IsNumber(n) ? n->valuedouble : 0.0;
}

static inline int ct_json_node_int64(cJSON *n, long long *out)
{
    double v;
    long long ll;

    if (!n || !out || !cJSON_IsNumber(n)) {
        return -1;
    }
    v = n->valuedouble;
    if (v < -9007199254740992.0 || v > 9007199254740992.0) {
        return -1;
    }
    ll = (long long)v;
    if ((double)ll != v) {
        return -1;
    }
    *out = ll;
    return 0;
}

static inline char *ct_json_node_string(cJSON *n)
{
    return cJSON_IsString(n) ? n->valuestring : 0;
}

static inline char *ct_json_node_key(cJSON *n)
{
    return n ? n->string : 0;
}

static inline int ct_json_node_count(cJSON *n)
{
    return n ? cJSON_GetArraySize(n) : 0;
}

static inline cJSON *ct_json_node_first_child(cJSON *n)
{
    return n ? n->child : 0;
}

static inline cJSON *ct_json_node_next_sibling(cJSON *n)
{
    return n ? n->next : 0;
}

static inline cJSON *ct_json_node_child_at(cJSON *n, int index)
{
    if (!n || index < 0) {
        return 0;
    }
    return cJSON_GetArrayItem(n, index);
}

static inline cJSON *ct_json_node_child_by_key(cJSON *n, const char *key)
{
    if (!n || !key) {
        return 0;
    }
    return cJSON_GetObjectItemCaseSensitive(n, key);
}

/* put and append return the receiver so a build chain keeps going; 0 means the
 * guard rejected the operation and the child was NOT consumed. */
static inline cJSON *ct_json_node_put(cJSON *obj, const char *key, cJSON *child)
{
    if (!obj || !key || !child || !cJSON_IsObject(obj)) {
        return 0;
    }
    if (cJSON_GetObjectItemCaseSensitive(obj, key)) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(obj, key, child)) {
            return 0;
        }
    } else if (!cJSON_AddItemToObject(obj, key, child)) {
        return 0;
    }
    return obj;
}

static inline cJSON *ct_json_node_append(cJSON *arr, cJSON *child)
{
    if (!arr || !child || !cJSON_IsArray(arr)) {
        return 0;
    }
    if (!cJSON_AddItemToArray(arr, child)) {
        return 0;
    }
    return arr;
}

/* Nodes are always borrowed from a doc; there is deliberately no node-level
 * free, because a cJSON node carries no owner flag and `cJSON_Delete` on a
 * borrowed node would double free when the doc is dropped. */

/* ------------------------------------------------------------------ */
/* builders (no receiver exists for creating a detached value)          */
/* ------------------------------------------------------------------ */

static inline cJSON *ct_json_new_null(void)
{
    return cJSON_CreateNull();
}

static inline cJSON *ct_json_new_bool(int v)
{
    return cJSON_CreateBool(v ? 1 : 0);
}

static inline cJSON *ct_json_new_number(double v)
{
    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
        return 0;
    }
    return cJSON_CreateNumber(v);
}

static inline cJSON *ct_json_new_string(const char *s)
{
    if (!s) {
        return 0;
    }
    return cJSON_CreateString(s);
}

static inline cJSON *ct_json_new_array(void)
{
    return cJSON_CreateArray();
}

static inline cJSON *ct_json_new_object(void)
{
    return cJSON_CreateObject();
}

/* ------------------------------------------------------------------ */
/* text entry points                                                   */
/* ------------------------------------------------------------------ */

static inline ct_json_doc *ct_json_text_to_doc(const char *text)
{
    ct_json_doc *d = ct_json_doc_new();
    if (!d) {
        return 0;
    }
    if (ct_json_doc_parse(d, text) != CT_JSON_OK) {
        ct_json_doc_free(d);
        return 0;
    }
    return d;
}

#endif
