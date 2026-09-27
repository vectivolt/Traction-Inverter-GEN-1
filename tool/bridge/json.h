/* json.h — the bridge's stdio format: newline-delimited JSON.
 * Input: one FLAT object per line ({"cmd":"torque","nm":120}); string, number, true/false/null values only.
 * Output: a line builder with automatic commas (objects and arrays nest up to 8 deep). */
#ifndef BRIDGE_JSON_H
#define BRIDGE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JIN_MAX_KEYS 24
#define JIN_KEY_LEN 32
#define JIN_VAL_LEN 512

typedef struct {
    int n;
    char key[JIN_MAX_KEYS][JIN_KEY_LEN];
    char val[JIN_MAX_KEYS][JIN_VAL_LEN]; /* strings unescaped; other tokens raw */
    bool is_str[JIN_MAX_KEYS];
} jin_t;

/* false: not a flat JSON object (the caller answers with an error) */
bool jin_parse(const char *line, jin_t *o);
const char *jin_str(const jin_t *o, const char *key); /* NULL if absent or not a string */
bool jin_num(const jin_t *o, const char *key, double *v); /* numbers (and numeric strings) */
bool jin_bool(const jin_t *o, const char *key, bool *v);

void jo_begin(void);                 /* starts a line: '{' */
void jo_end(void);                   /* '}' + '\n', written to stdout and flushed */
void jo_key(const char *k);          /* inside an object: "k": (pass NULL inside arrays) */
void jo_f(const char *k, float v);   /* non-finite -> null */
void jo_d(const char *k, double v);
void jo_i(const char *k, long long v);
void jo_u(const char *k, unsigned long long v);
void jo_b(const char *k, bool v);
void jo_s(const char *k, const char *s);
void jo_raw(const char *k, const char *raw);
void jo_obj(const char *k);          /* opens a nested object */
void jo_arr(const char *k);          /* opens a nested array */
void jo_close(void);                 /* closes the innermost object/array */

#endif /* BRIDGE_JSON_H */
