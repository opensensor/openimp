#ifndef OPENIMP_SYS_WRAP_JSON_H
#define OPENIMP_SYS_WRAP_JSON_H

#include <stddef.h>
#include <stdio.h>

typedef struct rcs {
    char *buf;
    int len;
    int cap;
} rcs;

typedef struct json_value {
    int type;
    char *text;
    struct json_value *next;
    struct json_value *prev;
    struct json_value *parent;
    struct json_value *child;
    struct json_value *tail;
} json_value;

typedef struct json_jpi {
    int state;
    int lexer_state;
    rcs *lexer_text;
    char *cursor_text;
    int reserved_10;
    size_t line;
    json_value *cursor;
} json_jpi;

typedef struct json_saxy_parser {
    void (*on_object_begin)(void);
    void (*on_object_end)(void);
    void (*on_array_begin)(void);
    void (*on_array_end)(void);
    void (*on_string)(char *);
    void (*on_number)(char *);
    void (*on_true)(void);
    void (*on_false)(void);
    void (*on_null)(void);
    void (*on_document_end)(void);
    void (*on_separator)(void);
} json_saxy_parser;

typedef struct json_saxy_state {
    int state;
    int overflow;
    rcs *text;
} json_saxy_state;

extern rcs *rcs_create(size_t size);
extern void rcs_free(rcs **rcs_ptr);
extern int rcs_resize(rcs *rcs_ptr, size_t size);
extern int rcs_catcs(rcs *pre, const char *pos, size_t len);
extern int rcs_catc(rcs *pre, char ch);
extern char *rcs_unwrap(rcs *rcs_ptr);
extern int rcs_length(rcs *rcs_ptr);

extern json_value *json_new_value(int type);
extern json_value *json_new_string(const char *text);
extern json_value *json_new_number(const char *text);
extern json_value *json_new_object(void);
extern json_value *json_new_array(void);
extern json_value *json_new_null(void);
extern json_value *json_new_true(void);
extern json_value *json_new_false(void);
extern void json_free_value(json_value **value);
extern int json_insert_child(json_value *parent, json_value *child);
extern int json_insert_pair_into_object(json_value *parent, const char *text_label, json_value *value);
extern int json_tree_to_string(json_value *root, char **text);
extern int json_stream_output(FILE *file, json_value *root);
extern char *json_strip_white_spaces(char *text);
extern char *json_format_string(const char *text);
extern char *json_escape(const char *text);
extern char *json_unescape(const char *text);
extern int json_jpi_init(json_jpi *jpi);
extern int lexer(char *buffer, char **p, int *state, rcs **text, size_t *line);
extern int json_parse_fragment(json_jpi *info, char *buffer);
extern int json_stream_parse(FILE *file, json_value **document);
extern int json_parse_document(json_value **root, char *text);
extern int json_saxy_parse(json_saxy_state *jsps, json_saxy_parser *jsf, char ch, json_saxy_state *state);
extern json_value *json_find_first_label(json_value *object, const char *text_label);
extern json_value *json_init(const char *arg1, const char *arg2);
extern int json_func(char *arg1);

#endif
