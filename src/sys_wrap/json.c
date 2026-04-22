#include "sys_wrap/json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void __assert(const char *assertion, const char *file, int line, const char *function);
extern int __fgetc_unlocked(FILE *stream);

rcs *rcs_create(size_t arg1)
{
    rcs *result = malloc(0xc);
    void *buf;

    if (result == NULL) {
        return NULL;
    }

    result->cap = (int)arg1;
    result->len = 0;
    buf = malloc(arg1 + 1);
    result->buf = buf;
    if (buf == NULL) {
        free(result);
        return NULL;
    }

    *(char *)buf = 0;
    return result;
}

void rcs_free(rcs **arg1)
{
    rcs *a0;

    if (arg1 == NULL) {
        __assert("rcs != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x59, "rcs_free");
    }

    a0 = *arg1;
    if (a0 == NULL) {
        return;
    }

    if (a0->buf != NULL) {
        free(a0->buf);
    }

    (*arg1)->buf = NULL;
    a0 = *arg1;
    free(a0);
    *arg1 = NULL;
}

int rcs_resize(rcs *arg1, size_t arg2)
{
    void *buf;

    if (arg1 == NULL) {
        __assert("rcs != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x6c, "rcs_resize");
    }

    buf = realloc(arg1->buf, arg2 + 1);
    if (buf == NULL) {
        free(arg1);
        return 0;
    }

    arg1->buf = buf;
    arg1->cap = (int)arg2;
    *((char *)buf + arg2) = 0;
    return 1;
}

int rcs_catcs(rcs *arg1, const char *arg2, size_t arg3)
{
    int v1;
    int a1;
    int result;

    if (arg1 == NULL) {
        __assert("pre != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x7e, "rcs_catcs");
    }

    if (arg2 == NULL) {
        __assert("pos != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x7f, "rcs_catcs");
    }

    v1 = arg1->len;
    a1 = v1 + (int)arg3;
    if ((unsigned int)arg1->cap < (unsigned int)a1) {
        result = 0;
        if (rcs_resize(arg1, (size_t)(a1 + 5)) == 1) {
            v1 = arg1->len;
        } else {
            return result;
        }
    }

    strncpy(arg1->buf + v1, arg2, arg3);
    result = 1;
    *(arg1->buf + arg3 + arg1->len) = 0;
    arg1->len += (int)arg3;
    return result;
}

int rcs_catc(rcs *arg1, char arg2)
{
    int v1;
    int a1;
    int result;
    char *v1_2;

    if (arg1 == NULL) {
        __assert("pre != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x90, "rcs_catc");
    }

    v1 = arg1->len;
    a1 = arg1->cap;
    if ((unsigned int)v1 >= (unsigned int)a1) {
        result = 0;
        if (rcs_resize(arg1, (size_t)(a1 + 5)) == 1) {
            v1 = arg1->len;
        } else {
            return result;
        }
    }

    result = 1;
    *(arg1->buf + v1) = arg2;
    v1 = arg1->len + 1;
    v1_2 = arg1->buf + v1;
    arg1->len = v1;
    *v1_2 = 0;
    return result;
}

char *rcs_unwrap(rcs *arg1)
{
    char *str;
    char *result;

    if (arg1 == NULL) {
        __assert("rcs != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xa2, "rcs_unwrap");
    }

    str = arg1->buf;
    if (str == NULL) {
        result = NULL;
    } else {
        result = realloc(str, strlen(str) + 1);
    }

    free(arg1);
    return result;
}

int rcs_length(rcs *arg1)
{
    if (arg1 != NULL) {
        return arg1->len;
    }

    __assert("rcs != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xb5, "rcs_length");
    return 0;
}

json_value *json_new_value(int arg1)
{
    json_value *result = malloc(0x1c);

    if (result != NULL) {
        memset((char *)result + 4, 0, 0x18);
        result->type = arg1;
    }

    return result;
}

json_value *json_new_string(const char *arg1)
{
    json_value *result;
    size_t size;
    char *text;

    if (arg1 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x10c, "json_new_string");
    }

    result = malloc(0x1c);
    if (result == NULL) {
        return NULL;
    }

    size = strlen(arg1) + 1;
    text = malloc(size);
    result->text = text;
    if (text == NULL) {
        free(result);
        return NULL;
    }

    strncpy(text, arg1, size);
    memset(&result->next, 0, 0x14);
    result->type = 0;
    return result;
}

json_value *json_new_number(const char *arg1)
{
    json_value *result;
    size_t size;
    char *text;

    if (arg1 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x12c, "json_new_number");
    }

    result = malloc(0x1c);
    if (result == NULL) {
        return NULL;
    }

    size = strlen(arg1) + 1;
    text = malloc(size);
    result->text = text;
    if (text == NULL) {
        free(result);
        return NULL;
    }

    strncpy(text, arg1, size);
    result->type = 1;
    memset(&result->next, 0, 0x14);
    return result;
}

json_value *json_new_object(void)
{
    return json_new_value(2);
}

json_value *json_new_array(void)
{
    return json_new_value(3);
}

json_value *json_new_null(void)
{
    return json_new_value(6);
}

json_value *json_new_true(void)
{
    return json_new_value(4);
}

json_value *json_new_false(void)
{
    return json_new_value(5);
}

void json_free_value(json_value **arg1)
{
    json_value *s0;
    json_value *v1_1;

    s0 = *arg1;
    if (s0 == NULL) {
        return;
    }

    v1_1 = s0;
    while (1) {
        json_value *v0;

        if (v1_1 == NULL) {
            return;
        }

        while (1) {
            v0 = s0->child;
            if (v0 == NULL) {
                break;
            }

            s0 = v0;
            if (v1_1 == NULL) {
                return;
            }
        }

        {
            json_value *v0_1;
            json_value *s2_1;
            json_value *v1_2;
            char *a0;
            json_value *a0_1;

            if (v1_1 == s0) {
                *arg1 = NULL;
                if (v1_1->child != NULL) {
                    __assert("(*value)->child == ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x16e, "intern_json_free_value");
                }
                s2_1 = v1_1->parent;
                v0_1 = s2_1;
            } else {
                v0_1 = s0->parent;
                s2_1 = v0_1;
            }

            v1_2 = s0->prev;
            if (v1_2 == NULL) {
                goto label_e51cc;
            }

            a0 = (char *)s0->next;
            if (a0 == NULL) {
                v1_2->next = NULL;
                goto label_e51cc;
            }

            v1_2->next = (json_value *)a0;
            ((json_value *)a0)->prev = v1_2;

label_e51cc:
            a0_1 = s0->next;
            if (a0_1 != NULL) {
                a0_1->prev = NULL;
            }

            if (v0_1 != NULL) {
                if (s0 == v0_1->child) {
                    v0_1->child = a0_1;
                }
                if (s0 == v0_1->tail) {
                    v0_1->tail = s0->prev;
                }
            }

            if (s0->text != NULL) {
                free(s0->text);
            }

            free(s0);
            s0 = s2_1;
            v1_1 = *arg1;
        }
    }
}

int json_insert_child(json_value *arg1, json_value *arg2)
{
    int v0;
    int v1_1;
    int result;
    int v1_2;
    json_value *v0_1;
    json_value *v1_4;

    if (arg1 == NULL) {
        __assert("parent != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x1cc, "json_insert_child");
    }

    if (arg2 == NULL) {
        __assert("child != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x1cd, "json_insert_child");
    }

    if (arg1 == arg2) {
        __assert("parent != child", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x1ce, "json_insert_child");
    }

    v0 = arg1->type;
    if (v0 == 2) {
        if (arg2->type != 0) {
            return 8;
        }
        v0_1 = arg1->child;
        goto label_e52e8;
    }

    if (v0 != 3 && v0 != 0) {
        return 8;
    }

    v1_1 = arg2->type;
    result = 8;
    if ((unsigned int)v1_1 < 7U) {
        v1_2 = 1 << (v1_1 & 0x1f);
        if ((v1_2 & 0x73) != 0) {
            if (arg2->child == NULL) {
                v0_1 = arg1->child;
                goto label_e52e8;
            }
        } else if ((v1_2 & 0xc) != 0) {
            v0_1 = arg1->child;
            goto label_e52e8;
        }
    }

    return result;

label_e52e8:
    arg2->parent = arg1;
    if (v0_1 == NULL) {
        arg1->child = arg2;
        arg1->tail = arg2;
        return 1;
    }

    v1_4 = arg1->tail;
    result = 1;
    arg2->prev = v1_4;
    v1_4->next = arg2;
    arg1->tail = arg2;
    return result;
}

int json_insert_pair_into_object(json_value *arg1, const char *arg2, json_value *arg3)
{
    json_value *v0;
    int result;

    if (arg1 == NULL) {
        __assert("parent != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x221, "json_insert_pair_into_object");
    }
    if (arg2 == NULL) {
        __assert("text_label != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x222, "json_insert_pair_into_object");
    }
    if (arg3 == NULL) {
        __assert("value != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x223, "json_insert_pair_into_object");
    }
    if (arg1 == arg3) {
        __assert("parent != value", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x224, "json_insert_pair_into_object");
    }
    if (arg1->type != 2) {
        __assert("parent->type == JSON_OBJECT", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x227, "json_insert_pair_into_object");
    }

    v0 = json_new_string(arg2);
    if (v0 == NULL) {
        return 6;
    }

    result = json_insert_child(v0, arg3);
    if (result == 1) {
        return json_insert_child(arg1, v0);
    }

    return result;
}

int json_tree_to_string(json_value *arg1, char **arg2)
{
    rcs *var_30;
    json_value *s0;

    if (arg1 == NULL) {
        __assert("root != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x242, "json_tree_to_string");
    }
    if (arg2 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x243, "json_tree_to_string");
    }

    var_30 = rcs_create(8);
    s0 = arg1;

    while (1) {
        int v0_2;
        int v0_3;
        int v0_13;

        if (s0->prev != NULL && s0 != arg1) {
            v0_2 = rcs_catc(var_30, 0x2c);
        } else {
            v0_2 = 1;
        }

        if (s0->prev == NULL || s0 == arg1 || v0_2 == 1) {
            v0_3 = s0->type;
            if ((unsigned int)v0_3 >= 7U) {
                break;
            }

            switch (v0_3) {
                case 0:
                    if (rcs_catc(var_30, 0x22) == 1) {
                        char *str_1 = s0->text;
                        int v0_28 = (int)strlen(str_1);

                        if (rcs_catcs(var_30, str_1, (size_t)v0_28) == 1 && rcs_catc(var_30, 0x22) == 1) {
                            json_value *v0_31 = s0->parent;

                            if (v0_31 != NULL && v0_31->type != 2) {
                                goto label_e5740;
                            }
                            if (s0->child == NULL) {
                                rcs_free(&var_30);
                                return 8;
                            }
                            if (rcs_catc(var_30, 0x3a) == 1) {
                                goto label_e5740;
                            }
                        }
                    }
                    return 6;

                case 1:
                {
                    char *str = s0->text;
                    int v0_24 = (int)strlen(str);

                    if (rcs_catcs(var_30, str, (size_t)v0_24) == 1) {
                        goto label_e564c;
                    }
                    return 6;
                }

                case 2:
                    if (rcs_catc(var_30, 0x7b) == 1) {
                        goto label_e5740;
                    }
                    return 6;

                case 3:
                    if (rcs_catc(var_30, 0x5b) == 1) {
                        goto label_e5740;
                    }
                    return 6;

                case 4:
                    if (rcs_catcs(var_30, "true", 4) == 1) {
                        int v0_18 = s0->type;

                        if ((unsigned int)v0_18 < 7U) {
                            v0_13 = 1 << (v0_18 & 0x1f);
                            goto label_e5660;
                        }
                    }
                    break;

                case 5:
                    if (rcs_catcs(var_30, "false", 5) == 1) {
                        goto label_e564c;
                    }
                    return 6;

                case 6:
                    if (rcs_catcs(var_30, "null", 4) == 1) {
                        goto label_e564c;
                    }
                    return 6;
            }
        }

        return 6;

label_e5740:
        if (s0->child == NULL) {
            goto label_e564c;
        }
        s0 = s0->child;
        continue;

label_e564c:
        while (1) {
            int v0_12 = s0->type;

            if ((unsigned int)v0_12 >= 7U) {
                goto label_e56ec;
            }

            v0_13 = 1 << (v0_12 & 0x1f);
label_e5660:
            if ((v0_13 & 0x73) == 0) {
                if ((v0_13 & 8) == 0) {
                    if ((v0_13 & 4) == 0) {
                        goto label_e56ec;
                    }
                    if (rcs_catc(var_30, 0x7d) != 1) {
                        return 6;
                    }
                } else if (rcs_catc(var_30, 0x5d) != 1) {
                    return 6;
                }
            }

            if (s0->parent == NULL || s0 == arg1) {
                *arg2 = rcs_unwrap(var_30);
                return 1;
            }

            s0 = s0->next;
            if (s0 != NULL) {
                break;
            }

            s0 = s0->parent;
        }
    }

label_e56ec:
    rcs_free(&var_30);
    return 0xa;
}

int json_stream_output(FILE *arg1, json_value *arg2)
{
    json_value *s0;

    if (arg2 == NULL) {
        __assert("root != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x326, "json_stream_output");
    }
    if (arg1 == NULL) {
        __assert("file != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x327, "json_stream_output");
    }

    s0 = arg2;
    while (1) {
        int v0_2;

        if (s0->prev != NULL && s0 != arg2) {
            fputc(0x2c, arg1);
        }

        v0_2 = s0->type;
        if ((unsigned int)v0_2 >= 7U) {
            return 0xa;
        }

        switch (v0_2) {
            case 0:
            {
                json_value *v0_14;
                json_value *v0_15;

                fprintf(arg1, "\"%s\"", s0->text);
                v0_14 = s0->parent;
                if (v0_14 == NULL) {
                    if (s0->child == NULL) {
                        return 8;
                    }
                } else if (v0_14->type != 2) {
                    v0_15 = s0->child;
                    if (v0_15 != NULL) {
                        s0 = v0_15;
                        continue;
                    }
                    return 8;
                }

                if (s0->child == NULL) {
                    return 8;
                }

                fputc(0x3a, arg1);
                v0_15 = s0->child;
                if (v0_15 != NULL) {
                    s0 = v0_15;
                    continue;
                }
                return 8;
            }

            case 1:
                fputs(s0->text, arg1);
                break;

            case 2:
                fputc(0x7b, arg1);
                if (s0->child != NULL) {
                    s0 = s0->child;
                    continue;
                }
                break;

            case 3:
                fputc(0x5b, arg1);
                if (s0->child != NULL) {
                    s0 = s0->child;
                    continue;
                }
                break;

            case 4:
                fwrite("true", 1, 4, arg1);
                break;

            case 5:
                fwrite("false", 1, 5, arg1);
                break;

            case 6:
                fwrite("null", 1, 4, arg1);
                break;
        }

        while (1) {
            int v0_8 = s0->type;
            int v0_9;
            json_value *v0_7;

            if ((unsigned int)v0_8 >= 7U) {
                return 0xa;
            }

            v0_9 = 1 << (v0_8 & 0x1f);
            if ((v0_9 & 0x73) != 0) {
                v0_7 = s0->parent;
                if (v0_7 != NULL) {
                    if (s0 != arg2) {
                        s0 = s0->next;
                        if (s0 != NULL) {
                            break;
                        }
                        s0 = v0_7;
                        continue;
                    }
                } else {
                    fputc(0x0a, arg1);
                    return 1;
                }
            } else if ((v0_9 & 8) == 0) {
                if ((v0_9 & 4) == 0) {
                    return 0xa;
                }
                fputc(0x7d, arg1);
                v0_7 = s0->parent;
                if (v0_7 != NULL) {
                    goto label_e5a0c;
                }
                fputc(0x0a, arg1);
                return 1;
            } else {
                fputc(0x5d, arg1);
                v0_7 = s0->parent;
                if (v0_7 != NULL) {
label_e5a0c:
                    if (s0 != arg2) {
                        s0 = s0->next;
                        if (s0 != NULL) {
                            break;
                        }
                        s0 = v0_7;
                        continue;
                    }
                } else {
                    fputc(0x0a, arg1);
                    return 1;
                }
            }
        }
    }
}

char *json_strip_white_spaces(char *arg1)
{
    char *result;
    int a2;
    char *a0;
    int t1_1;

    if (arg1 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x3dc, "json_strip_white_spaces");
    }

    result = (char *)strlen(arg1);
    a2 = 0;
    if (result != 0) {
        a0 = arg1;
        result += (uintptr_t)arg1;
        t1_1 = 0;
        do {
            char a1_1 = *a0;
            uint32_t v1_2 = (uint32_t)(uint8_t)(a1_1 - 9);

            if (v1_2 < 0x1a) {
                int v1_3 = 1 << (v1_2 & 0x1f);

                if ((v1_3 & 0x800013) != 0) {
                    if (t1_1 == 1) {
                        arg1[a2] = a1_1;
                        a2 += 1;
                    }
                    t1_1 = 0;
                    goto next_char;
                }

                if ((v1_3 & 0x2000000) != 0) {
                    char *v1_5 = &arg1[a2];
                    int temp1_2 = t1_1;

                    t1_1 = 1;
                    if (temp1_2 != 1) {
                        *v1_5 = a1_1;
                        a2 += 1;
                    }
                    t1_1 = (((unsigned char)*(a0 - 1)) ^ 0x5c) < 1 ? 1 : 0;
                    goto next_char;
                }
            }

            arg1[a2] = a1_1;
            a2 += 1;

next_char:
            a0 = &a0[1];
        } while ((uintptr_t)a0 != (uintptr_t)result);
        arg1[a2] = 0;
    }

    return result;
}

char *json_format_string(const char *arg1)
{
    size_t v0;
    rcs *v0_1;
    int s2_1;
    int s0_1;

    v0 = strlen(arg1);
    v0_1 = rcs_create(v0);
    if (v0 == 0) {
        return rcs_unwrap(v0_1);
    }

    s2_1 = 0;
    s0_1 = 0;
    while (1) {
        int i = (int)(signed char)arg1[s0_1];
        if (i == 0x22) {
            rcs_catc(v0_1, 0x22);
            s0_1 += 1;
            while (1) {
                int i_1 = (int)(signed char)arg1[s0_1];

                while (i_1 != 0x5c) {
                    rcs_catc(v0_1, (char)i_1);
                    s0_1 += 1;
                    if ((unsigned int)s0_1 >= (unsigned int)v0) {
                        return rcs_unwrap(v0_1);
                    }
                    if (i_1 == 0x22) {
                        goto next_outer;
                    }
                    i_1 = (int)(signed char)arg1[s0_1];
                }

                rcs_catc(v0_1, 0x5c);
                {
                    int s7_1 = s0_1 + 1;
                    int a1_2 = (int)(signed char)arg1[s7_1];

                    if (a1_2 == 0x22) {
                        rcs_catc(v0_1, 0x22);
                        s7_1 = s0_1 + 2;
                        a1_2 = (int)(signed char)arg1[s7_1];
                    }

                    rcs_catc(v0_1, (char)a1_2);
                    s0_1 = s7_1 + 1;
                    if ((unsigned int)s0_1 >= (unsigned int)v0) {
                        return rcs_unwrap(v0_1);
                    }
                }
            }
        }

        if (i < 0x23) {
            if (i == 0x0d) {
                s0_1 += 1;
                goto next_outer;
            }
            if (i < 0x0e) {
                if ((unsigned int)(i - 9) <= 1U) {
                    rcs_catc(v0_1, (char)i);
                }
                s0_1 += 1;
                goto next_outer;
            }
            if (i == 0x20) {
                s0_1 += 1;
                goto next_outer;
            }

            rcs_catc(v0_1, (char)i);
            s0_1 += 1;
            goto next_outer;
        }

        if (i == 0x3a) {
            rcs_catcs(v0_1, ": ", 2);
            s0_1 += 1;
            goto next_outer;
        }

        if (i < 0x3b) {
            if (i != 0x2c) {
                rcs_catc(v0_1, (char)i);
                s0_1 += 1;
                goto next_outer;
            }

            rcs_catcs(v0_1, ",\n", 2);
            if (s2_1 == 0) {
                s0_1 += 1;
                goto next_outer;
            }

            for (int s5_2 = 0; s5_2 != s2_1; s5_2 += 1) {
                rcs_catc(v0_1, 9);
            }

            s0_1 += 1;
            goto next_outer;
        }

        if (i == 0x7b) {
            rcs_catcs(v0_1, "{\n", 2);
            s2_1 += 1;
            if (s2_1 == 0) {
                s0_1 += 1;
                goto next_outer;
            }

            for (int s5_3 = 0; s2_1 != s5_3; s5_3 += 1) {
                rcs_catc(v0_1, 9);
            }

            s0_1 += 1;
            goto next_outer;
        }

        if (i != 0x7d) {
            rcs_catc(v0_1, (char)i);
            s0_1 += 1;
            goto next_outer;
        }

        s2_1 -= 1;
        rcs_catc(v0_1, 0x0a);
        if (s2_1 != 0) {
            for (int s5_1 = 0; s2_1 != s5_1; s5_1 += 1) {
                rcs_catc(v0_1, 9);
            }
        }
        rcs_catc(v0_1, 0x7d);
        s0_1 += 1;
        if ((unsigned int)s0_1 >= (unsigned int)v0) {
            return rcs_unwrap(v0_1);
        }

next_outer:
        if ((unsigned int)s0_1 >= (unsigned int)v0) {
            return rcs_unwrap(v0_1);
        }
    }
}

char *json_escape(const char *arg1)
{
    const char *s0;
    int v0;
    rcs *v0_1;
    const char *s2_1;
    char var_30[8];

    if (arg1 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x479, "json_escape");
    }

    s0 = arg1;
    v0 = (int)strlen(arg1);
    v0_1 = rcs_create(v0);
    if (v0_1 == NULL) {
        return NULL;
    }

    s2_1 = &s0[v0];
    if (v0 != 0) {
        while (1) {
            int a1_2 = (int)(signed char)*s0;

            if (a1_2 == 0x5c) {
                rcs_catcs(v0_1, "\\\\", 2);
                goto step_char;
            }
            if (a1_2 == 0x22) {
                rcs_catcs(v0_1, "\\\"", 2);
                goto step_char;
            }
            if (a1_2 == 0x2f) {
                rcs_catcs(v0_1, "\\/", 2);
                goto step_char;
            }
            if (a1_2 == 8) {
                rcs_catcs(v0_1, "\\b", 2);
                goto step_char;
            }
            if (a1_2 == 0x0c) {
                rcs_catcs(v0_1, "\\f", 2);
                goto step_char;
            }
            if (a1_2 == 0x0a) {
                rcs_catcs(v0_1, "\\n", 2);
                goto step_char;
            }
            if (a1_2 == 0x0d) {
                rcs_catcs(v0_1, "\\r", 2);
                goto step_char;
            }
            if (a1_2 == 9) {
                rcs_catcs(v0_1, "\\t", 2);
                goto step_char;
            }
            if (a1_2 >= 0 && a1_2 < 0x20) {
                sprintf(var_30, "\\u%4.4x", a1_2);
                rcs_catcs(v0_1, var_30, 6);
                goto step_char;
            }

            rcs_catc(v0_1, (char)a1_2);

step_char:
            s0 = &s0[1];
            if (s0 == s2_1) {
                break;
            }
        }
    }

    return rcs_unwrap(v0_1);
}

char *json_unescape(const char *arg1)
{
    const char *s3 = arg1;
    int s1 = 0;
    char *result = malloc(strlen(arg1) + 1);
    int i = (int)(signed char)*s3;

    if (i != 0) {
        int s0_1 = 0;

        do {
            if (i != 0x5c) {
                *(result + s1) = (char)i;
                s1 += 1;
            } else {
                char a0_1 = s3[s0_1 + 1];
                uint32_t v0_3 = (uint32_t)(uint8_t)(a0_1 - 0x22);

                if (v0_3 >= 0x54) {
                    __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x541, "json_unescape");
                }

                switch (v0_3) {
                    case 0:
                    case 0xd:
                    case 0x3a:
                        *(result + s1) = a0_1;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x40:
                        *(result + s1) = 8;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x44:
                        *(result + s1) = 0x0c;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x4c:
                        *(result + s1) = 0x0a;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x50:
                        *(result + s1) = 0x0d;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x52:
                        *(result + s1) = 9;
                        s1 += 1;
                        s0_1 += 1;
                        break;

                    case 0x53:
                    {
                        const char *fp_1 = &s3[s0_1];
                        char str[5];
                        long v0_10;
                        char *v1_3;

                        str[0] = *(fp_1 + 2);
                        str[1] = *(fp_1 + 3);
                        str[2] = *(fp_1 + 4);
                        str[3] = s3[s0_1 + 5];
                        str[4] = 0;
                        v0_10 = strtol(str, NULL, 0x10);
                        if (v0_10 < 0) {
                            v1_3 = result + s1;
                            *v1_3 = (char)v0_10;
                            s1 += 1;
                            s0_1 += 5;
                            break;
                        }
                        v1_3 = result + s1;
                        if ((unsigned long)v0_10 < 0x80UL) {
                            *v1_3 = (char)v0_10;
                            s1 += 1;
                            s0_1 += 5;
                            break;
                        }
                        if ((unsigned long)v0_10 < 0x800UL) {
                            char *v1_11 = result + s1 + 1;
                            *(result + s1) = (char)((unsigned long)v0_10 >> 6) - 0x40;
                            s1 += 2;
                            *v1_11 = ((char)v0_10 & 0x3f) - 0x80;
                            s0_1 += 5;
                            break;
                        }
                        if ((unsigned long)v0_10 >= 0x10000UL) {
                            fprintf(stderr, "JSON: unsupported unicode value: 0x%lX\n", v0_10);
                            s0_1 += 5;
                            break;
                        }
                        if ((unsigned long)(v0_10 - 0xd800) < 0x400UL) {
                            int v0_24 = s0_1 + 6;

                            if ((int)(signed char)s3[v0_24] == 0x5c) {
                                v0_24 = s0_1 + 7;
                                if ((int)(signed char)s3[v0_24] == 0x75) {
                                    int a0_10;
                                    int a1_5;
                                    int v0_30;
                                    int v1_18;
                                    int a0_14;
                                    char *a3_3;
                                    char *a2_3;
                                    char str2[5];
                                    long v0_27;

                                    s0_1 += 0xb;
                                    str2[0] = *(fp_1 + 8);
                                    str2[1] = *(fp_1 + 9);
                                    str2[2] = *(fp_1 + 0xa);
                                    str2[3] = s3[s0_1];
                                    str2[4] = 0;
                                    v0_27 = strtol(str2, NULL, 0x10);
                                    a3_3 = result + s1;
                                    a0_10 = ((int)v0_10 - 0xd800) << 10;
                                    a1_5 = a0_10 + 0x2400 + (int)v0_27;
                                    v0_30 = (((unsigned int)a1_5 < (unsigned int)(a0_10 + 0x2400)) ? 1 : 0)
                                          + (((unsigned int)(a0_10 + 0x2400) < (unsigned int)a0_10) ? 1 : 0)
                                          + (((unsigned int)((int)v0_10 - 0xd800)) >> 22)
                                          + (((int)v0_27) >> 31);
                                    v1_18 = (v0_30 << 26) | ((unsigned int)a1_5 >> 6);
                                    a0_14 = ((((int)v0_30) >> 6) << 26) | ((unsigned int)v1_18 >> 6);
                                    a2_3 = result + s1 + 3;
                                    *a3_3 = (char)(((unsigned int)a0_14 >> 6) & 7) - 0x10;
                                    a3_3[1] = (a0_14 & 0x3f) - 0x80;
                                    a3_3[2] = (v1_18 & 0x3f) - 0x80;
                                    s1 += 4;
                                    *a2_3 = (a1_5 & 0x3f) - 0x80;
                                    break;
                                }
                                s0_1 = v0_24;
                            } else {
                                s0_1 = v0_24;
                            }
                        }

                        {
                            int v1_8 = s1 + 2;
                            char *a2_1 = result + s1;

                            *a2_1 = (char)((unsigned long)v0_10 >> 12) - 0x20;
                            a2_1[1] = (char)(((unsigned long)v0_10 >> 6) & 0x3f) - 0x80;
                            s1 += 3;
                            *(result + v1_8) = ((char)v0_10 & 0x3f) - 0x80;
                            s0_1 += 5;
                        }
                        break;
                    }

                    default:
                        __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x541, "json_unescape");
                        break;
                }
            }

            s0_1 += 1;
            i = (int)(signed char)s3[s0_1];
        } while (i != 0);
    }

    *(result + s1) = 0;
    return result;
}

int json_jpi_init(json_jpi *arg1)
{
    if (arg1 == NULL) {
        __assert("jpi != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x553, "json_jpi_init");
    }

    memset(arg1, 0, 0x14);
    arg1->cursor = NULL;
    arg1->line = 1;
    return 1;
}

int lexer(char *arg1, char **arg2, int *arg3, rcs **arg4, size_t *arg5)
{
    char *v1_1;

    if (arg1 == NULL) {
        __assert("buffer != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x561, "lexer");
    }
    if (arg2 == NULL) {
        __assert("p != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x562, "lexer");
    }
    if (arg3 == NULL) {
        __assert("state != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x563, "lexer");
    }
    if (arg4 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x564, "lexer");
    }
    if (arg5 == NULL) {
        __assert("line != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x565, "lexer");
    }

    v1_1 = *arg2;
    if (v1_1 == NULL) {
        *arg2 = arg1;
        v1_1 = arg1;
    }

    while (1) {
        int a1 = (int)(signed char)*v1_1;
        int a2;

        if (a1 == 0) {
            *arg2 = NULL;
            return 0;
        }

        a2 = *arg3;
        if ((unsigned int)a2 >= 0x19U) {
            fprintf(stderr, "JSON: *state missing: %d\n", a2);
            return 1;
        }

        switch (a2) {
            case 0:
            {
                uint32_t v0_43;

                *arg2 = &v1_1[1];
                v0_43 = (uint32_t)(uint8_t)(*v1_1 - 9);
                if (v0_43 >= 0x75) {
                    return 1;
                }

                switch (v0_43) {
                    case 0:
                    case 0x17:
                        v1_1 = &v1_1[1];
                        continue;

                    case 1:
                    case 4:
                        v1_1 = &v1_1[1];
                        *arg5 += 1;
                        continue;

                    case 0x19:
                        *arg4 = rcs_create(8);
                        if (*arg4 == NULL) {
                            return 0xe;
                        }
                        *arg3 = 1;
                        v1_1 = *arg2;
                        continue;

                    case 0x23:
                        return 0xa;

                    case 0x24:
                        *arg4 = rcs_create(8);
                        if (*arg4 != NULL && rcs_catc(*arg4, 0x2d) == 1) {
                            *arg3 = 0x11;
                            v1_1 = *arg2;
                            continue;
                        }
                        return 0xe;

                    case 0x27:
                        *arg4 = rcs_create(8);
                        if (*arg4 != NULL && rcs_catc(*arg4, 0x30) == 1) {
                            *arg3 = 0x12;
                            v1_1 = *arg2;
                            continue;
                        }
                        return 0xe;

                    case 0x28:
                    case 0x29:
                    case 0x2a:
                    case 0x2b:
                    case 0x2c:
                    case 0x2d:
                    case 0x2e:
                    case 0x2f:
                    case 0x30:
                        *arg4 = rcs_create(8);
                        if (*arg4 != NULL && rcs_catc(*arg4, (char)(signed char)*(*arg2 - 1)) == 1) {
                            *arg3 = 0x13;
                            v1_1 = *arg2;
                            continue;
                        }
                        return 0xe;

                    case 0x31:
                        return 9;
                    case 0x52:
                        return 7;
                    case 0x54:
                        return 8;
                    case 0x5d:
                        *arg3 = 0xa;
                        v1_1 = &v1_1[1];
                        continue;
                    case 0x65:
                        *arg3 = 0xe;
                        v1_1 = &v1_1[1];
                        continue;
                    case 0x6b:
                        *arg3 = 7;
                        v1_1 = &v1_1[1];
                        continue;
                    case 0x72:
                        return 5;
                    case 0x74:
                        return 6;
                    default:
                        return 1;
                }
            }

            case 1:
            {
                rcs *a0_2 = *arg4;

                if (a0_2 == NULL) {
                    break;
                }
                if (a1 == 0x22) {
                    *arg3 = 0;
                    *arg2 = &v1_1[1];
                    return 0xb;
                }
                if (a1 < 0x23) {
                    if ((unsigned int)(a1 - 1) > 0x1eU) {
                        goto label_e6910;
                    }
                    return 1;
                }
                if (a1 != 0x5c) {
                    goto label_e6910;
                }
                if (rcs_catc(a0_2, 0x5c) != 1) {
                    return 0xe;
                }
                *arg3 = 2;
label_e6928:
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                continue;
            }

            case 2:
            {
                rcs *a0_9 = *arg4;
                uint32_t v0_36;
                int v0_86;

                if (a0_9 == NULL) {
                    break;
                }
                v0_36 = (uint32_t)(uint8_t)(a1 - 0x22);
                if (v0_36 >= 0x54) {
                    return 1;
                }
                switch (v0_36) {
                    case 0:
                    case 0xd:
                    case 0x3a:
                    case 0x40:
                    case 0x44:
                    case 0x4c:
                    case 0x50:
                    case 0x52:
                        v0_86 = rcs_catc(a0_9, (char)a1);
                        if (v0_86 != 1) {
                            return 0xe;
                        }
                        goto label_e6928;

                    case 0x53:
                        if (rcs_catc(a0_9, 0x75) != 1) {
                            return 0xe;
                        }
                        v0_86 = 3;
                        *arg3 = v0_86;
                        goto label_e6928;

                    default:
                        return 1;
                }
            }

            case 3:
            {
                int v0_15 = a1 & 0xff;

                if (*arg4 == NULL) {
                    break;
                }
                if ((unsigned int)(v0_15 - 0x61) >= 6U &&
                    (unsigned int)(v0_15 - 0x41) >= 6U &&
                    (unsigned int)(v0_15 - 0x30) >= 0xaU) {
                    return 1;
                }
                if (rcs_catc(*arg4, (char)a1) != 1) {
                    return 0xe;
                }
                *arg3 = 4;
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                continue;
            }

            case 4:
            {
                int v0_19 = a1 & 0xff;

                if (*arg4 == NULL) {
                    break;
                }
                if ((unsigned int)(v0_19 - 0x61) >= 6U &&
                    (unsigned int)(v0_19 - 0x41) >= 6U &&
                    (unsigned int)(v0_19 - 0x30) >= 0xaU) {
                    return 1;
                }
                if (rcs_catc(*arg4, (char)a1) != 1) {
                    return 0xe;
                }
                *arg3 = 5;
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                continue;
            }

            case 5:
            {
                int v0_23 = a1 & 0xff;

                if (*arg4 == NULL) {
                    break;
                }
                if ((unsigned int)(v0_23 - 0x61) >= 6U &&
                    (unsigned int)(v0_23 - 0x41) >= 6U &&
                    (unsigned int)(v0_23 - 0x30) >= 0xaU) {
                    return 1;
                }
                if (rcs_catc(*arg4, (char)a1) != 1) {
                    return 0xe;
                }
                *arg3 = 6;
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                continue;
            }

            case 6:
            {
                int v0_27 = a1 & 0xff;

                if (*arg4 == NULL) {
                    break;
                }
                if ((unsigned int)(v0_27 - 0x61) >= 6U &&
                    (unsigned int)(v0_27 - 0x41) >= 6U &&
                    (unsigned int)(v0_27 - 0x30) >= 0xaU) {
                    return 1;
                }
                if (rcs_catc(*arg4, (char)a1) != 1) {
                    return 0xe;
                }
                *arg3 = 1;
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                continue;
            }

            case 7:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x72) {
                    return 1;
                }
                *arg3 = 8;
                v1_1 = &v1_1[1];
                continue;

            case 8:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x75) {
                    return 1;
                }
                *arg3 = 9;
                v1_1 = &v1_1[1];
                continue;

            case 9:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x65) {
                    return 1;
                }
                *arg3 = 0;
                return 2;

            case 0xa:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x61) {
                    return 1;
                }
                *arg3 = 0xb;
                v1_1 = &v1_1[1];
                continue;

            case 0xb:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x6c) {
                    return 1;
                }
                *arg3 = 0xc;
                v1_1 = &v1_1[1];
                continue;

            case 0xc:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x73) {
                    return 1;
                }
                *arg3 = 0xd;
                v1_1 = &v1_1[1];
                continue;

            case 0xd:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x65) {
                    return 1;
                }
                *arg3 = 0;
                return 3;

            case 0xe:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x75) {
                    return 1;
                }
                *arg3 = 0xf;
                v1_1 = &v1_1[1];
                continue;

            case 0xf:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x6c) {
                    return 1;
                }
                *arg3 = 0x10;
                v1_1 = &v1_1[1];
                continue;

            case 0x10:
                *arg2 = &v1_1[1];
                if ((int)(signed char)*v1_1 != 0x6c) {
                    return 1;
                }
                *arg3 = 0;
                return 4;

            case 0x11:
            {
                rcs *a0_12 = *arg4;

                if (a0_12 == NULL) {
                    break;
                }
                if (a1 == 0x30) {
                    if (rcs_catc(a0_12, 0x30) != 1) {
                        return 0xe;
                    }
                    v1_1 = *arg2 + 1;
                    *arg2 = v1_1;
                    *arg3 = 0x12;
                    continue;
                }
                if (a1 < 0x30 || a1 >= 0x3a) {
                    return 1;
                }
                if (rcs_catc(*arg4, (char)a1) != 1) {
                    return 0xe;
                }
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                *arg3 = 0x13;
                continue;
            }

            case 0x12:
            {
                rcs *a0_2 = *arg4;

                if (a0_2 == NULL) {
                    break;
                }
                if (a1 == 0x2c || a1 == 0x5d) {
                    *arg3 = 0;
                    return 0xc;
                }
                if (a1 < 0x2d) {
                    goto label_e6f38;
                }
                if (a1 >= 0x5e) {
                    goto label_e707c;
                }
                if (a1 == 0x2e) {
                    if (rcs_catc(a0_2, 0x2e) != 1) {
                        return 0xe;
                    }
                    v1_1 = *arg2 + 1;
                    *arg2 = v1_1;
                    *arg3 = 0x14;
                    continue;
                }
                return 1;
            }

            case 0x13:
            {
                rcs *a0_2 = *arg4;
                uint32_t v0_57;

                if (a0_2 == NULL) {
                    break;
                }
                v0_57 = (uint32_t)(uint8_t)(a1 - 9);
                if (v0_57 >= 0x75) {
                    return 1;
                }
                switch (v0_57) {
                    case 0:
                    case 1:
                    case 4:
                    case 0x17:
                        *arg2 = &v1_1[1];
                        *arg3 = 0;
                        return 0xc;

                    case 0x23:
                    case 0x54:
                    case 0x74:
                        *arg3 = 0;
                        return 0xc;

                    case 0x25:
                        if (rcs_catc(a0_2, 0x2e) != 1) {
                            return 0xe;
                        }
                        v1_1 = *arg2 + 1;
                        *arg2 = v1_1;
                        *arg3 = 0x14;
                        continue;

                    case 0x27:
                    case 0x28:
                    case 0x29:
                    case 0x2a:
                    case 0x2b:
                    case 0x2c:
                    case 0x2d:
                    case 0x2e:
                    case 0x2f:
                    case 0x30:
label_e6910:
                        if (rcs_catc(a0_2, (char)a1) != 1) {
                            return 0xe;
                        }
                        goto label_e6928;

                    case 0x3c:
                    case 0x5c:
label_e6f98:
                        if (rcs_catc(a0_2, (char)a1) != 1) {
                            return 0xe;
                        }
                        v1_1 = *arg2 + 1;
                        *arg2 = v1_1;
                        *arg3 = 0x16;
                        continue;

                    default:
                        return 1;
                }
            }

            case 0x14:
            {
                rcs *a0_13 = *arg4;

                if (a0_13 == NULL) {
                    break;
                }
                if ((unsigned int)(a1 - 0x30) > 9U) {
                    return 1;
                }
                if (rcs_catc(a0_13, (char)a1) != 1) {
                    return 0xe;
                }
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                *arg3 = 0x15;
                continue;
            }

            case 0x15:
            {
                rcs *a0_2 = *arg4;

                if (a0_2 == NULL) {
                    break;
                }
                if (a1 >= 0x3a) {
                    if (a1 == 0x5d) {
                        *arg3 = 0;
                        return 0xc;
                    }
                    if (a1 >= 0x5e) {
                        goto label_e707c;
                    }
                    if (a1 != 0x45) {
                        return 1;
                    }
                    goto label_e6f98;
                }
                if (a1 >= 0x30) {
                    goto label_e6910;
                }
                if (a1 == 0x0d) {
                    goto label_e6f64;
                }
                if (a1 < 0x0e) {
label_e6f54:
                    if ((unsigned int)(a1 - 9) > 1U) {
                        return 1;
                    }
                    goto label_e6f64;
                }
                if (a1 == 0x20) {
                    goto label_e6f64;
                }
                if (a1 != 0x2c) {
                    return 1;
                }
                *arg3 = 0;
                return 0xc;
            }

            case 0x16:
            {
                rcs *a0_1 = *arg4;
                int v0_5;

                if (a0_1 == NULL) {
                    break;
                }
                if (a1 == 0x2d) {
label_e6df8:
                    if (rcs_catc(a0_1, (char)a1) != 1) {
                        return 0xe;
                    }
                    v1_1 = *arg2 + 1;
                    *arg2 = v1_1;
                    *arg3 = 0x17;
                    continue;
                }
                v0_5 = a1 - 0x30;
                if (a1 < 0x2e) {
                    if (a1 != 0x2b) {
                        return 1;
                    }
                    goto label_e6df8;
                }
                if ((unsigned int)v0_5 > 9U) {
                    return 1;
                }
                if (rcs_catc(a0_1, (char)a1) != 1) {
                    return 0xe;
                }
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                *arg3 = 0x18;
                continue;
            }

            case 0x17:
            {
                rcs *a0_1 = *arg4;
                int v0_5;

                if (a0_1 == NULL) {
                    break;
                }
                v0_5 = a1 - 0x30;
                if ((unsigned int)v0_5 > 9U) {
                    return 1;
                }
                if (rcs_catc(a0_1, (char)a1) != 1) {
                    return 0xe;
                }
                v1_1 = *arg2 + 1;
                *arg2 = v1_1;
                *arg3 = 0x18;
                continue;
            }

            case 0x18:
            {
                rcs *a0_2 = *arg4;

                if (a0_2 == NULL) {
                    break;
                }
                if (a1 == 0x2c || a1 == 0x5d) {
                    *arg3 = 0;
                    return 0xc;
                }
                if (a1 < 0x2d) {
label_e6f38:
                    if (a1 == 0x0d) {
                        goto label_e6f64;
                    }
                    if (a1 < 0x0e) {
                        goto label_e6f54;
                    }
                    if (a1 != 0x20) {
                        return 1;
                    }
                    *arg2 = &v1_1[1];
                    *arg3 = 0;
                    return 0xc;
                }
                if (a1 < 0x5e) {
                    if ((unsigned int)(a1 - 0x30) > 9U) {
                        return 1;
                    }
                    goto label_e6910;
                }
label_e707c:
                if (a1 == 0x65) {
                    goto label_e6f98;
                }
label_e7088:
                if (a1 != 0x7d) {
                    return 1;
                }
                *arg3 = 0;
                return 0xc;
            }

            default:
                break;
        }
        break;

label_e6f64:
        *arg2 = &v1_1[1];
        *arg3 = 0;
        return 0xc;
    }

    __assert("*text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0, "lexer");
    return 1;
}

int json_parse_fragment(json_jpi *arg1, char *arg2)
{
    int s1_1;
    char *v0_1;

    if (arg1 == NULL) {
        __assert("info != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x85e, "json_parse_fragment");
    }
    if (arg2 == NULL) {
        __assert("buffer != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x85f, "json_parse_fragment");
    }

    s1_1 = arg1->state;
    arg1->cursor_text = arg2;
    v0_1 = arg2;

    while (1) {
        if ((int)(signed char)*v0_1 == 0) {
            arg1->cursor_text = NULL;
            if (s1_1 != 0x63) {
                return 2;
            }
            return 3;
        }

        if ((unsigned int)s1_1 >= 0x64U) {
            fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", s1_1, arg1->line);
            return 0xa;
        }

        switch (s1_1) {
            case 0:
            {
                int v0_15 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                if (v0_15 == 1) {
                    return 4;
                }
                s1_1 = 1;
                if (v0_15 != 5) {
                    fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                    return 4;
                }
                arg1->state = 1;
                v0_1 = arg1->cursor_text;
                continue;
            }

            case 1:
                if (arg1->cursor == NULL) {
                    arg1->cursor = json_new_object();
                    if (arg1->cursor == NULL) {
                        return 6;
                    }
                } else {
                    int v0_17 = arg1->cursor->type;
                    if (v0_17 == 0 || v0_17 == 3) {
                        json_value *v0_18 = json_new_object();
                        if (v0_18 == NULL) {
                            return 6;
                        }
                        if (json_insert_child(arg1->cursor, v0_18) != 1) {
                            return 0xa;
                        }
                        arg1->cursor = v0_18;
                    } else {
                        __assert("(info->cursor->type == JSON_STRING) || (info->cursor->type == JSON_ARRAY)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x886, "json_parse_fragment");
                    }
                }
                arg1->state = 2;
                s1_1 = 2;
                v0_1 = arg1->cursor_text;
                continue;

            case 2:
                if (arg1->cursor == NULL) {
                    __assert("info->cursor != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x89a, "json_parse_fragment");
                }
                s1_1 = arg1->cursor->type;
                if (s1_1 != 2) {
                    __assert("info->cursor->type == JSON_OBJECT", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0, "json_parse_fragment");
                }
                {
                    int v0_22 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_22 == 6) {
                        json_value *v0_58 = json_new_value(0);
                        char *v0_59;

                        if (v0_58 == NULL) {
                            return 6;
                        }
                        v0_59 = rcs_unwrap(arg1->lexer_text);
                        v0_58->text = v0_59;
                        arg1->lexer_text = NULL;
                        if (json_insert_child(arg1->cursor, v0_58) != 1) {
                            return 0xa;
                        }
                        arg1->cursor = v0_58;
                        arg1->state = 5;
                        s1_1 = 5;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                    if (v0_22 == 0xb) {
                        json_value *v0_84;

                        v0_84 = arg1->cursor->parent;
                        if (v0_84 == NULL) {
                            arg1->state = 0x63;
                            s1_1 = 0x63;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        s1_1 = v0_84->type;
                        arg1->cursor = v0_84;
                        if (s1_1 != 0) {
                            if (s1_1 != 3) {
                                return 8;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        if (v0_84->parent == NULL) {
                            arg1->state = 0x63;
                            s1_1 = 0x63;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        s1_1 = v0_84->parent->type;
                        arg1->cursor = v0_84->parent;
                        if (s1_1 != 2) {
                            return 8;
                        }
                        arg1->state = 3;
                        s1_1 = 3;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                    if (v0_22 == 0) {
                        return 2;
                    }
                    fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                    return 4;
                }

            case 3:
                if (arg1->cursor == NULL) {
                    __assert("info->cursor != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x8e0, "json_parse_fragment");
                }
                if (arg1->cursor->type != 2) {
                    __assert("info->cursor->type == JSON_OBJECT", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0, "json_parse_fragment");
                }
                {
                    int v0_22 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_22 == 0xa) {
                        arg1->state = 4;
                        s1_1 = 4;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                    if (v0_22 == 6) {
                        fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                        return 4;
                    }
                    if (v0_22 == 0) {
                        return 2;
                    }
                    fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                    return 4;
                }

            case 4:
                if (arg1->cursor != NULL) {
                    if (arg1->cursor->type != 2) {
                        __assert("info->cursor->type == JSON_OBJECT", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x91a, "json_parse_fragment");
                    }
                }
                {
                    int v0_22 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_22 == 1) {
                        return 7;
                    }
                    if (v0_22 != 0xb) {
                        if (v0_22 == 0) {
                            return 2;
                        }
                        fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                        return 4;
                    }

                    {
                        json_value *v0_28 = json_new_value(0);
                        char *v0_29;

                        if (v0_28 == NULL) {
                            return 6;
                        }
                        v0_29 = rcs_unwrap(arg1->lexer_text);
                        v0_28->text = v0_29;
                        arg1->lexer_text = NULL;
                        if (json_insert_child(arg1->cursor, v0_28) != 1) {
                            return 0xa;
                        }
                        arg1->state = 5;
                        arg1->cursor = v0_28;
                        s1_1 = 5;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                }

            case 5:
                if (arg1->cursor != NULL) {
                    if (arg1->cursor->type != 0) {
                        __assert("info->cursor->type == JSON_STRING", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x940, "json_parse_fragment");
                    }
                }
                {
                    int v0_34 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_34 == 0) {
                        return 2;
                    }
                    if (v0_34 != 9) {
                        fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                        return 4;
                    }
                    arg1->state = 6;
                    s1_1 = 6;
                    v0_1 = arg1->cursor_text;
                    continue;
                }

            case 6:
                if (arg1->cursor != NULL) {
                    if (arg1->cursor->type != 0) {
                        __assert("info->cursor->type == JSON_STRING", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x959, "json_parse_fragment");
                    }
                }
                {
                    int v0_38 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if ((unsigned int)v0_38 >= 0xfU) {
                        fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                        return 4;
                    }

                    switch (v0_38) {
                        case 0:
                            return 2;
                        case 1:
                            return 7;
                        case 2:
                        {
                            json_value *v0_62 = json_new_value(4);
                            if (v0_62 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_62) != 1) {
                                return 0xa;
                            }
                            break;
                        }
                        case 3:
                        {
                            json_value *v0_62 = json_new_value(5);
                            if (v0_62 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_62) != 1) {
                                return 0xa;
                            }
                            break;
                        }
                        case 4:
                        {
                            json_value *v0_62 = json_new_value(6);
                            if (v0_62 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_62) != 1) {
                                return 0xa;
                            }
                            break;
                        }
                        case 5:
                            arg1->state = 1;
                            s1_1 = 1;
                            v0_1 = arg1->cursor_text;
                            continue;
                        case 7:
                            arg1->state = 7;
                            s1_1 = 7;
                            v0_1 = arg1->cursor_text;
                            continue;
                        case 0xb:
                        {
                            json_value *s1_5 = json_new_value(0);
                            char *v0_67;

                            if (s1_5 == NULL) {
                                return 6;
                            }
                            v0_67 = rcs_unwrap(arg1->lexer_text);
                            s1_5->text = v0_67;
                            arg1->lexer_text = NULL;
                            if (json_insert_child(arg1->cursor, s1_5) != 1) {
                                return 0xa;
                            }
                            break;
                        }
                        case 0xc:
                        {
                            json_value *s1_5 = json_new_value(1);
                            char *v0_67;

                            if (s1_5 == NULL) {
                                return 6;
                            }
                            v0_67 = rcs_unwrap(arg1->lexer_text);
                            s1_5->text = v0_67;
                            arg1->lexer_text = NULL;
                            if (json_insert_child(arg1->cursor, s1_5) != 1) {
                                return 0xa;
                            }
                            break;
                        }
                        case 0xe:
                            return 6;
                        default:
                            fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                            return 4;
                    }

                    if (arg1->cursor->parent != NULL) {
                        arg1->cursor = arg1->cursor->parent;
                    }
                    arg1->state = 3;
                    s1_1 = 3;
                    v0_1 = arg1->cursor_text;
                    continue;
                }

            case 7:
                if (arg1->cursor == NULL) {
                    arg1->cursor = json_new_array();
                    if (arg1->cursor == NULL) {
                        return 6;
                    }
                } else {
                    int v0_53 = arg1->cursor->type;
                    if (v0_53 == 3 || v0_53 == 0) {
                        json_value *v0_54 = json_new_array();
                        if (v0_54 == NULL) {
                            return 6;
                        }
                        if (json_insert_child(arg1->cursor, v0_54) != 1) {
                            return 0xa;
                        }
                        arg1->cursor = v0_54;
                    } else {
                        __assert("(info->cursor->type == JSON_ARRAY) || (info->cursor->type == JSON_STRING)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x9eb, "json_parse_fragment");
                    }
                }
                arg1->state = 8;
                s1_1 = 8;
                v0_1 = arg1->cursor_text;
                continue;

            case 8:
                if (arg1->cursor == NULL) {
                    __assert("info->cursor != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x9ff, "json_parse_fragment");
                }
                if (arg1->cursor->type != 3) {
                    __assert("info->cursor->type == JSON_ARRAY", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xa00, "json_parse_fragment");
                }
                {
                    int v0_45 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if ((unsigned int)v0_45 >= 0xdU) {
                        fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                        return 4;
                    }
                    switch (v0_45) {
                        case 0:
                            return 2;
                        case 1:
                            return 7;
                        case 2:
                        {
                            json_value *v0_76 = json_new_value(4);
                            if (v0_76 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_76) != 1) {
                                return 0xa;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        case 3:
                        {
                            json_value *v0_76 = json_new_value(5);
                            if (v0_76 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_76) != 1) {
                                return 0xa;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        case 4:
                        {
                            json_value *v0_76 = json_new_value(6);
                            if (v0_76 == NULL) {
                                return 6;
                            }
                            if (json_insert_child(arg1->cursor, v0_76) != 1) {
                                return 0xa;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        case 5:
                            arg1->state = 1;
                            s1_1 = 1;
                            v0_1 = arg1->cursor_text;
                            continue;
                        case 7:
                            arg1->state = 7;
                            s1_1 = 7;
                            v0_1 = arg1->cursor_text;
                            continue;
                        case 8:
                            if (arg1->cursor->parent == NULL) {
                                arg1->state = 0x63;
                                s1_1 = 0x63;
                                v0_1 = arg1->cursor_text;
                                continue;
                            }
                            s1_1 = arg1->cursor->parent->type;
                            arg1->cursor = arg1->cursor->parent;
                            if (s1_1 != 0) {
                                if (s1_1 != 3) {
                                    return 8;
                                }
                                arg1->state = 9;
                                s1_1 = 9;
                                v0_1 = arg1->cursor_text;
                                continue;
                            }
                            if (arg1->cursor->parent == NULL) {
                                return 8;
                            }
                            arg1->cursor = arg1->cursor->parent;
                            if (arg1->cursor->type != 2) {
                                return 8;
                            }
                            arg1->state = 3;
                            s1_1 = 3;
                            v0_1 = arg1->cursor_text;
                            continue;
                        case 0xb:
                        {
                            json_value *s1_6 = json_new_value(0);
                            char *v0_71;

                            if (s1_6 == NULL) {
                                return 6;
                            }
                            v0_71 = rcs_unwrap(arg1->lexer_text);
                            s1_6->text = v0_71;
                            arg1->lexer_text = NULL;
                            if (json_insert_child(arg1->cursor, s1_6) != 1) {
                                return 0xa;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        case 0xc:
                        {
                            json_value *s1_6 = json_new_value(1);
                            char *v0_71;

                            if (s1_6 == NULL) {
                                return 6;
                            }
                            v0_71 = rcs_unwrap(arg1->lexer_text);
                            s1_6->text = v0_71;
                            arg1->lexer_text = NULL;
                            if (json_insert_child(arg1->cursor, s1_6) != 1) {
                                return 0xa;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        default:
                            fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                            return 4;
                    }
                }

            case 9:
                if (arg1->cursor != NULL) {
                    int v0_22 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_22 == 8) {
                        if (arg1->cursor->parent == NULL) {
                            arg1->state = 0x63;
                            s1_1 = 0x63;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        s1_1 = arg1->cursor->parent->type;
                        arg1->cursor = arg1->cursor->parent;
                        if (s1_1 != 0) {
                            if (s1_1 != 3) {
                                return 8;
                            }
                            arg1->state = 9;
                            s1_1 = 9;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        if (arg1->cursor->parent == NULL) {
                            arg1->state = 0x63;
                            s1_1 = 0x63;
                            v0_1 = arg1->cursor_text;
                            continue;
                        }
                        arg1->cursor = arg1->cursor->parent;
                        if (arg1->cursor->type != 2) {
                            return 8;
                        }
                        arg1->state = 3;
                        s1_1 = 3;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                    if (v0_22 == 0xa) {
                        arg1->state = 8;
                        s1_1 = 8;
                        v0_1 = arg1->cursor_text;
                        continue;
                    }
                    if (v0_22 == 0) {
                        return 2;
                    }
                }
                fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", arg1->state, arg1->line);
                return 4;

            case 0x63:
                if (arg1->cursor->parent != NULL) {
                    __assert("info->cursor->parent == ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xab6, "json_parse_fragment");
                }
                {
                    int v0_12 = lexer(arg2, &arg1->cursor_text, &arg1->lexer_state, &arg1->lexer_text, &arg1->line);

                    if (v0_12 == 0) {
                        return 3;
                    }
                    if (v0_12 == 0xe) {
                        return 6;
                    }
                    return 4;
                }

            default:
                fprintf(stderr, "JSON: state %d: defaulted at line %zd\n", s1_1, arg1->line);
                return 0xa;
        }
    }
}

int json_stream_parse(FILE *arg1, json_value **arg2)
{
    json_jpi var_38;
    int s0;
    int v0_8;

    if (arg1 == NULL) {
        __assert("file != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xc5, "json_stream_parse");
    }
    if (arg2 == NULL) {
        __assert("document != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xc6, "json_stream_parse");
    }
    if (*arg2 != NULL) {
        __assert("*document == ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xc7, "json_stream_parse");
    }

    json_jpi_init(&var_38);
    s0 = 2;
    while (1) {
        char str[0x400];
        json_value *var_20 = NULL;
        if (fgets(str, 0x400, arg1) == NULL) {
            v0_8 = 0xa;
            if (s0 != 3) {
                break;
            }
        } else {
            int v0_3 = json_parse_fragment(&var_38, str);
            s0 = v0_3;
            if ((unsigned int)(v0_3 - 1) >= 3U) {
                json_free_value(&var_20);
                return s0;
            }
            if ((unsigned int)(s0 - 2) < 2U) {
                continue;
            }
            v0_8 = s0;
            if (s0 != 1) {
                break;
            }
            *arg2 = var_20;
            return 1;
        }

        return v0_8;
    }

    return v0_8;
}

int json_parse_document(json_value **arg1, char *arg2)
{
    json_jpi *v0_2;
    int v0_4;
    int v0_5;

    if (arg1 == NULL) {
        __assert("root != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xadc, "json_parse_document");
    }
    if (*arg1 != NULL) {
        __assert("*root == ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xadd, "json_parse_document");
    }
    if (arg2 == NULL) {
        __assert("text != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xade, "json_parse_document");
    }

    v0_2 = malloc(0x1c);
    if (v0_2 == NULL) {
        return 6;
    }

    json_jpi_init(v0_2);
    v0_4 = json_parse_fragment(v0_2, arg2);
    v0_5 = v0_4 & 0xfffffffd;
    if (v0_5 != 1) {
        free(v0_2);
        return v0_4;
    }

    *arg1 = v0_2->cursor;
    free(v0_2);
    return v0_5;
}

int json_saxy_parse(json_saxy_state *arg1, json_saxy_parser *arg2, char arg3, json_saxy_state *arg4)
{
    json_saxy_parser *a1_3;

    if (arg1 == NULL) {
        __assert("jsps != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xafb, "json_saxy_parse");
    }
    if (arg2 == NULL) {
        __assert("jsf != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0xafc, "json_saxy_parse");
    }

    if ((unsigned int)arg1->state >= 0x1cU) {
        return 0xa;
    }

    switch (arg1->state) {
        case 0:
        {
            uint32_t v0_54 = (uint32_t)(uint8_t)((signed char)arg3 - 9);

            if (v0_54 >= 0x75) {
                return 7;
            }

            switch (v0_54) {
                case 0:
                case 1:
                case 4:
                case 0x17:
                    return 1;
                case 0x19:
                    arg1->overflow = 0;
                    arg1->state = 1;
                    return 1;
                case 0x23:
                    if (arg2->on_separator != NULL) {
                        arg2->on_separator();
                    }
                    arg1->state = 0x1b;
                    return 1;
                case 0x24:
                    arg1->overflow = 0;
                    arg1->state = 0x17;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x2d) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x27:
                    arg1->overflow = 0;
                    arg1->state = 0x11;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x30) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x28:
                case 0x29:
                case 0x2a:
                case 0x2b:
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f:
                case 0x30:
                    arg1->overflow = 0;
                    arg1->state = 0x18;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, arg3) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x31:
                    if (arg2->on_document_end == NULL) {
                        return 1;
                    }
                    arg2->on_document_end();
                    return 1;
                case 0x52:
                    if (arg2->on_array_begin == NULL) {
                        return 1;
                    }
                    arg2->on_array_begin();
                    return 1;
                case 0x54:
                    if (arg2->on_array_end != NULL) {
                        arg2->on_array_end();
                    } else {
                        arg1->state = 0x1a;
                    }
                    arg1->state = 0x1a;
                    return 1;
                case 0x5d:
                    arg1->state = 0xa;
                    return 1;
                case 0x65:
                    arg1->state = 0xe;
                    return 1;
                case 0x6b:
                    arg1->state = 7;
                    return 1;
                case 0x72:
                    if (arg2->on_object_begin != NULL) {
                        arg2->on_object_begin();
                    }
                    arg1->state = 0x19;
                    return 1;
                case 0x74:
                    if (arg2->on_object_end != NULL) {
                        arg2->on_object_end();
                    } else {
                        arg1->state = 0x1a;
                    }
                    arg1->state = 0x1a;
                    return 1;
                default:
                    return 7;
            }
        }

        case 1:
            if ((signed char)arg3 == 0x22) {
                if (arg1->text == NULL) {
                    return 0xa;
                }
                arg1->state = 0;
                if (arg2->on_string != NULL) {
                    arg2->on_string(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                return 1;
            }
            if ((signed char)arg3 != 0x5c) {
                if (arg1->overflow != 0) {
                    return 1;
                }
                if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                    arg1->overflow = 1;
                    return 1;
                }
                if (rcs_catc(arg1->text, arg3) == 1) {
                    return 1;
                }
                return 6;
            }
            if (arg1->overflow == 0) {
                if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffdU) {
                    arg1->overflow = 1;
                } else if (rcs_catc(arg1->text, 0x5c) != 1) {
                    return 6;
                }
            }
            arg1->state = 2;
            return 1;

        case 2:
        {
            uint32_t v0_48 = (uint32_t)(uint8_t)((signed char)arg3 - 0x22);

            if (v0_48 >= 0x54) {
                return 7;
            }
            switch (v0_48) {
                case 0:
                case 0xd:
                case 0x3a:
                case 0x40:
                case 0x44:
                case 0x4c:
                case 0x50:
                case 0x52:
                    if (arg1->overflow != 0) {
                        return 1;
                    }
                    if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                        goto label_overflow_1;
                    }
                    if (rcs_catc(arg1->text, arg3) == 1) {
                        return 1;
                    }
                    return 6;
                case 0x53:
                    if (arg1->overflow == 0) {
                        if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffaU) {
                            arg1->overflow = 1;
                        } else if (rcs_catc(arg1->text, 0x75) != 1) {
                            return 6;
                        }
                    }
                    arg1->state = 3;
                    return 1;
                default:
                    return 7;
            }
        }

        case 3:
            switch ((uint8_t)((signed char)arg3 - 0x30)) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0x11:
                case 0x12:
                case 0x13:
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x34:
                case 0x35:
                case 0x36:
                    if (arg1->overflow == 0) {
                        if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffbU) {
                            arg1->overflow = 1;
                        } else if (rcs_catc(arg1->text, 0x75) != 1) {
                            return 6;
                        }
                    }
                    arg1->state = 4;
                    return 1;
                default:
                    return 7;
            }

        case 4:
            switch ((uint8_t)((signed char)arg3 - 0x30)) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0x11:
                case 0x12:
                case 0x13:
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x34:
                case 0x35:
                case 0x36:
                    if (arg1->overflow == 0) {
                        if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffcU) {
                            arg1->overflow = 1;
                        } else if (rcs_catc(arg1->text, arg3) != 1) {
                            return 6;
                        }
                    }
                    arg1->state = 5;
                    return 1;
                default:
                    return 7;
            }

        case 5:
            switch ((uint8_t)((signed char)arg3 - 0x30)) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0x11:
                case 0x12:
                case 0x13:
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x34:
                case 0x35:
                case 0x36:
                    if (arg1->overflow == 0) {
                        if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffdU) {
                            arg1->overflow = 1;
                        } else if (rcs_catc(arg1->text, arg3) != 1) {
                            return 6;
                        }
                    }
                    arg1->state = 6;
                    return 1;
                default:
                    return 7;
            }

        case 6:
        {
            uint32_t v0_27 = (uint32_t)(uint8_t)((signed char)arg3 - 0x30);

            if (v0_27 >= 0x37) {
                return 7;
            }
            switch (v0_27) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0x11:
                case 0x12:
                case 0x13:
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x34:
                case 0x35:
                case 0x36:
                    if (arg1->overflow == 0) {
                        if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                            arg1->overflow = 1;
                        } else if (rcs_catc(arg1->text, arg3) != 1) {
                            return 6;
                        }
                    }
                    arg1->state = 1;
                    return 1;
                default:
                    return 7;
            }
        }

        case 7:
            if ((signed char)arg3 != 0x72) {
                return 7;
            }
            arg1->state = 8;
            return 1;

        case 8:
            if ((signed char)arg3 != 0x75) {
                return 7;
            }
            arg1->state = 9;
            return 1;

        case 9:
            if ((signed char)arg3 != 0x65) {
                return 7;
            }
            arg1->state = 0;
            if (arg2->on_true != NULL) {
                arg2->on_true();
            }
            return 1;

        case 0xa:
            if ((signed char)arg3 != 0x61) {
                return 7;
            }
            arg1->state = 0xb;
            return 1;

        case 0xb:
            if ((signed char)arg3 != 0x6c) {
                return 7;
            }
            arg1->state = 0xc;
            return 1;

        case 0xc:
            if ((signed char)arg3 != 0x73) {
                return 7;
            }
            arg1->state = 0xd;
            return 1;

        case 0xd:
            if ((signed char)arg3 != 0x65) {
                return 7;
            }
            arg1->state = 0;
            if (arg2->on_false != NULL) {
                arg2->on_false();
            }
            return 1;

        case 0xe:
            if ((signed char)arg3 != 0x75) {
                return 7;
            }
            arg1->state = 0xf;
            return 1;

        case 0xf:
            if ((signed char)arg3 != 0x6c) {
                return 7;
            }
            arg1->state = 0x10;
            return 1;

        case 0x10:
            if ((signed char)arg3 != 0x6c) {
                return 7;
            }
            arg1->state = 0;
            if (arg2->on_null != NULL) {
                arg2->on_null();
            }
            return 1;

        case 0x11:
            if ((signed char)arg3 == 0x20 || ((signed char)arg3 < 0x21 && ((signed char)arg3 >= 9 && ((signed char)arg3 < 0xb || (signed char)arg3 == 0xd)))) {
                if (arg1->text == NULL) {
                    return 6;
                }
                if (arg2->on_number != NULL) {
                    arg2->on_number(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                arg1->state = 0;
                return 1;
            }
            if ((signed char)arg3 == 0x2e) {
                json_saxy_state *v0_101 = (json_saxy_state *)rcs_create(5);
                arg1->text = (rcs *)v0_101;
                if (v0_101 != NULL && rcs_catc((rcs *)v0_101, 0x2e) == 1) {
                    arg1->state = 0x12;
                    return 1;
                }
                return 6;
            }
            if ((signed char)arg3 >= 0x2f) {
                if ((signed char)arg3 == 0x5d) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    a1_3 = arg2;
                    if (a1_3->on_array_end == NULL) {
                        arg1->state = 0x1a;
                        return 1;
                    }
                    a1_3->on_array_end();
                    arg1->state = 0x1a;
                    return 1;
                }
                if ((signed char)arg3 != 0x7d) {
                    if ((signed char)arg3 != 0x2c) {
                        return 7;
                    }
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    if (arg2->on_separator != NULL) {
                        arg2->on_separator();
                    }
                    arg1->state = 0x1b;
                    return 1;
                }
                if (arg1->text == NULL) {
                    return 6;
                }
                if (arg2->on_number != NULL) {
                    arg2->on_number(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                if (arg2->on_object_end != NULL) {
                    arg2->on_object_end();
                } else {
                    arg1->state = 0x1a;
                    return 1;
                }
                arg1->state = 0x1a;
                return 1;
            }
            if ((unsigned int)((signed char)arg3 - 0x30) > 9U) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x18;
                return 1;
            }
            if (rcs_length(arg1->text) == -1) {
                arg1->overflow = 1;
            } else if (rcs_catc(arg1->text, arg3) != 1) {
                return 6;
            }
            arg1->state = 0x13;
            return 1;

        case 0x12:
            if ((unsigned int)((signed char)arg3 - 0x30) > 9U) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x13;
                return 1;
            }
            if (rcs_length(arg1->text) == -1) {
                arg1->overflow = 1;
            } else if (rcs_catc(arg1->text, arg3) != 1) {
                return 6;
            }
            arg1->state = 0x13;
            return 1;

        case 0x13:
            if ((signed char)arg3 < 0x3a && (signed char)arg3 >= 0x30) {
                if (arg1->overflow != 0) {
                    return 1;
                }
                if (rcs_length(arg1->text) == -1) {
label_overflow_1:
                    arg1->overflow = 1;
                    return 1;
                }
                if (rcs_catc(arg1->text, arg3) == 1) {
                    return 1;
                }
                return 6;
            }
            if ((signed char)arg3 == 0x0d || (signed char)arg3 == 0x20 || (((signed char)arg3 < 0x0e) && (unsigned int)((signed char)arg3 - 9) <= 1U)) {
                if (arg1->text == NULL) {
                    return 6;
                }
                if (arg2->on_number != NULL) {
                    arg2->on_number(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                arg1->state = 0;
                return 1;
            }
            if ((signed char)arg3 == 0x2c) {
                if (arg1->text == NULL) {
                    return 6;
                }
                if (arg2->on_number != NULL) {
                    arg2->on_number(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                if (arg2->on_separator != NULL) {
                    arg2->on_separator();
                }
                arg1->state = 0x1b;
                return 1;
            }
            if ((signed char)arg3 == 0x5d) {
                if (arg2->on_number != NULL) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    arg2->on_number(arg1->text->buf);
                    rcs_free(&arg1->text);
                    a1_3 = arg2;
                    if (a1_3->on_array_end == NULL) {
                        arg1->state = 0x1a;
                        return 1;
                    }
                    a1_3->on_array_end();
                    arg1->state = 0x1a;
                    return 1;
                }
                rcs_free(&arg1->text);
                arg1->state = 0x1a;
                return 1;
            }
            if ((signed char)arg3 < 0x5e) {
                if ((signed char)arg3 != 0x45) {
                    return 7;
                }
            } else if ((signed char)arg3 != 0x65) {
                if ((signed char)arg3 != 0x7d) {
                    return 7;
                }
                if (arg1->text == NULL) {
                    return 6;
                }
                if (arg2->on_number != NULL) {
                    arg2->on_number(arg1->text->buf);
                }
                rcs_free(&arg1->text);
                if (arg2->on_object_end != NULL) {
                    arg2->on_object_end();
                } else {
                    arg1->state = 0x1a;
                    return 1;
                }
                arg1->state = 0x1a;
                return 1;
            }
            if (rcs_catc(arg1->text, arg3) != 1) {
                return 6;
            }
            arg1->state = 0x14;
            return 1;

        case 0x14:
            if ((signed char)arg3 == 0x2d || (signed char)arg3 == 0x2b) {
                arg1->overflow = 0;
                if (rcs_catc(arg1->text, arg3) != 1) {
                    return 6;
                }
                arg1->state = 0x16;
                return 1;
            }
            if ((signed char)arg3 < 0x2e || (unsigned int)((signed char)arg3 - 0x30) > 9U) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x15;
                return 1;
            }
            if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                arg1->overflow = 1;
            } else if (rcs_catc(arg1->text, arg3) != 1) {
                return 6;
            }
            arg1->state = 0x15;
            return 1;

        case 0x15:
            if ((signed char)arg3 == 0x2c) {
                if (arg2->on_number != NULL) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    arg2->on_number(arg1->text->buf);
                    free(arg1->text);
                    arg1->text = NULL;
                    if (arg2->on_separator != NULL) {
                        arg2->on_separator();
                    }
                    arg1->state = 0x1b;
                    return 1;
                }
                free(arg1->text);
                arg1->text = NULL;
                arg1->state = 0x1b;
                return 1;
            }
            if ((signed char)arg3 == 0x5d) {
                if (arg2->on_number != NULL) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    arg2->on_number(arg1->text->buf);
                    free(arg1->text);
                    arg1->text = NULL;
                    a1_3 = arg2;
                    if (a1_3->on_array_end == NULL) {
                        arg1->state = 0x1a;
                        return 1;
                    }
                    a1_3->on_array_end();
                    arg1->state = 0x1a;
                    return 1;
                }
                free(arg1->text);
                arg1->text = NULL;
                arg1->state = 0x1a;
                return 1;
            }
            if ((signed char)arg3 < 0x2d) {
                if ((signed char)arg3 == 0x0d || (((signed char)arg3 < 0x0e) && (unsigned int)((signed char)arg3 - 9) <= 1U) || (signed char)arg3 == 0x20) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    arg1->state = 0;
                    return 1;
                }
                return 7;
            }
            if ((signed char)arg3 >= 0x5e) {
                if ((signed char)arg3 == 0x7d) {
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    if (arg2->on_object_end != NULL) {
                        arg2->on_object_end();
                    } else {
                        arg1->state = 0x1a;
                        return 1;
                    }
                    arg1->state = 0x1a;
                    return 1;
                }
                return 7;
            }
            if ((unsigned int)((signed char)arg3 - 0x30) > 9U) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x15;
                return 1;
            }
            if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                arg1->overflow = 1;
            } else {
                rcs_catc(arg1->text, arg3);
            }
            arg1->state = 0x15;
            return 1;

        case 0x16:
            if ((unsigned int)((signed char)arg3 - 0x30) > 9U) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x15;
                return 1;
            }
            if ((unsigned int)rcs_length(arg1->text) >= 0xfffffffeU) {
                arg1->overflow = 1;
            } else {
                rcs_catc(arg1->text, arg3);
            }
            arg1->state = 0x15;
            return 1;

        case 0x17:
            if ((signed char)arg3 == 0x30) {
                rcs_catc(arg1->text, 0x30);
                arg1->state = 0x11;
                return 1;
            }
            if ((signed char)arg3 < 0x30 || (signed char)arg3 >= 0x3a) {
                return 7;
            }
            if (arg1->overflow != 0) {
                arg1->state = 0x18;
                return 1;
            }
            if (rcs_length(arg1->text) == -1) {
                arg1->state = 0x18;
                return 1;
            }
            {
                rcs *v0_138 = rcs_create(5);

                arg1->text = v0_138;
                if (v0_138 != NULL) {
                    int v0_139 = rcs_catc(v0_138, arg3);

                    if (v0_139 == 1) {
                        arg1->overflow = v0_139;
                        arg1->state = 0x18;
                        return 1;
                    }
                }
                return 6;
            }

        case 0x18:
        {
            uint32_t v0_9 = (uint32_t)(uint8_t)((signed char)arg3 - 9);

            if (v0_9 >= 0x75) {
                return 7;
            }
            switch (v0_9) {
                case 0:
                case 1:
                case 4:
                case 0x17:
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    arg1->state = 0;
                    return 1;
                case 0x23:
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (arg2->on_number != NULL) {
                        arg2->on_number(arg1->text->buf);
                    }
                    rcs_free(&arg1->text);
                    if (arg2->on_separator != NULL) {
                        arg2->on_separator();
                    }
                    arg1->state = 0x1b;
                    return 1;
                case 0x24:
                    arg1->state = 0x17;
                    arg1->text = rcs_create(8);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x2d) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x27:
                    arg1->state = 0x11;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x30) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x28:
                case 0x29:
                case 0x2a:
                case 0x2b:
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f:
                case 0x30:
                    arg1->state = 0x18;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, arg3) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x52:
                    if (arg2->on_array_begin == NULL) {
                        return 1;
                    }
                    arg2->on_array_begin();
                    return 1;
                case 0x5d:
                    arg1->state = 0xa;
                    return 1;
                case 0x65:
                    arg1->state = 0xe;
                    return 1;
                case 0x6b:
                    arg1->state = 7;
                    return 1;
                case 0x72:
                    if (arg2->on_object_begin != NULL) {
                        arg2->on_object_begin();
                    }
                    arg1->state = 0x19;
                    return 1;
                default:
                    return 7;
            }
        }

        case 0x19:
            if ((signed char)arg3 == 0x0d || ((signed char)arg3 < 0x0e && (unsigned int)((signed char)arg3 - 9) <= 1U) || (signed char)arg3 == 0x20) {
                return 1;
            }
            if ((signed char)arg3 == 0x22) {
                arg1->text = NULL;
                arg1->state = 1;
                return 1;
            }
            if ((signed char)arg3 == 0x7d) {
                if (arg2->on_object_end != NULL) {
                    arg2->on_object_end();
                } else {
                    arg1->state = 0x1a;
                    return 1;
                }
                arg1->state = 0x1a;
                return 1;
            }
            return 7;

        case 0x1a:
            if ((signed char)arg3 == 0x20) {
                return 1;
            }
            if ((signed char)arg3 < 0x21) {
                if ((signed char)arg3 < 9) {
                    return 7;
                }
                if ((signed char)arg3 < 0xb) {
                    return 1;
                }
                if ((signed char)arg3 != 0x0d) {
                    return 7;
                }
                return 1;
            }
            if ((signed char)arg3 == 0x5d) {
                if (arg2->on_array_end == NULL) {
                    return 1;
                }
                arg2->on_array_end();
                return 1;
            }
            if ((signed char)arg3 != 0x7d) {
                if ((signed char)arg3 == 0x2c) {
                    if (arg2->on_separator != NULL) {
                        arg2->on_separator();
                    }
                    arg1->state = 0x1b;
                    return 1;
                }
                return 7;
            }
            if (arg2->on_object_end == NULL) {
                return 1;
            }
            arg2->on_object_end();
            return 1;

        case 0x1b:
        {
            uint32_t v0_61 = (uint32_t)(uint8_t)((signed char)arg3 - 9);

            if (v0_61 >= 0x73) {
                return 7;
            }

            switch (v0_61) {
                case 0:
                case 1:
                case 4:
                case 0x17:
                    return 1;
                case 0x19:
                    arg1->state = 1;
                    arg1->text = NULL;
                    return 1;
                case 0x24:
                    arg1->state = 0x17;
                    arg1->text = rcs_create(8);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x2d) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x27:
                    arg1->state = 0x11;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, 0x30) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x28:
                case 0x29:
                case 0x2a:
                case 0x2b:
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f:
                case 0x30:
                    arg1->state = 0x18;
                    arg1->text = rcs_create(5);
                    if (arg1->text == NULL) {
                        return 6;
                    }
                    if (rcs_catc(arg1->text, arg3) != 1) {
                        return 6;
                    }
                    return 1;
                case 0x52:
                    if (arg2->on_array_begin == NULL) {
                        return 1;
                    }
                    arg2->on_array_begin();
                    return 1;
                case 0x5d:
                    arg1->state = 0xa;
                    return 1;
                case 0x65:
                    arg1->state = 0xe;
                    return 1;
                case 0x6b:
                    arg1->state = 7;
                    return 1;
                case 0x72:
                    if (arg2->on_object_begin != NULL) {
                        arg2->on_object_begin();
                    }
                    arg1->state = 0x19;
                    return 1;
                default:
                    return 7;
            }
        }

        default:
            return 0xa;
    }
}

json_value *json_find_first_label(json_value *arg1, const char *arg2)
{
    json_value *i;

    if (arg1 == NULL) {
        __assert("object != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x106d, "json_find_first_label");
    }
    if (arg2 == NULL) {
        __assert("text_label != ((void *)0)", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x106e, "json_find_first_label");
    }
    if (arg1->type != 2) {
        __assert("object->type == JSON_OBJECT", "/home/user/git/proj/sdk-lv3/src/imp/sys_wrap/json.c", 0x106f, "json_find_first_label");
    }

    i = arg1->child;
    if (i != NULL) {
        do {
            if (strcmp(i->text, arg2) == 0) {
                break;
            }
            i = i->next;
        } while (i != NULL);
    }

    return i;
}

json_value *json_init(const char *arg1, const char *arg2)
{
    json_value *v0;
    json_value *v0_1;
    json_value *v0_3;

    v0 = json_new_object();
    v0_1 = json_new_string(arg1);
    json_insert_child(v0_1, json_new_string(arg2));
    json_insert_child(v0, v0_1);
    v0_3 = json_new_string(" ");
    json_insert_child(v0_3, json_new_string(" "));
    json_insert_child(v0, v0_3);
    v0_3 = json_new_string("entry");
    json_insert_child(v0_3, v0);
    return v0_3;
}

int json_func(char *arg1)
{
    char *var_2c = NULL;
    json_value *var_30 = NULL;

    if (access("/tmp/Settings.jz", 0) != 0) {
        var_30 = json_new_object();
        json_insert_child(var_30, json_init("Label", "Value"));
        json_tree_to_string(var_30, &var_2c);
        json_free_value(&var_30);
        json_parse_document(&var_30, var_2c);
    }

    {
        FILE *stream = fopen("/tmp/Settings.jz", "r");
        char str[0xc8];

        if (stream != NULL) {
            int s6_1 = 0;
            unsigned int i;

            if (*(int *)((char *)stream + 0x4c) == 0) {
                i = (unsigned int)fgetc(stream);
            } else {
                unsigned char *v1_1 = *(unsigned char **)((char *)stream + 0x10);

                if (v1_1 >= *(unsigned char **)((char *)stream + 0x18)) {
                    i = (unsigned int)__fgetc_unlocked(stream);
                } else {
                    *(unsigned char **)((char *)stream + 0x10) = &v1_1[1];
                    i = (unsigned int)*v1_1;
                }
            }

            if (i != 0xffffffffU) {
                unsigned char *s1_1 = (unsigned char *)str;

                *s1_1 = (unsigned char)i;
                if (*(int *)((char *)stream + 0x4c) == 0) {
                    i = (unsigned int)fgetc(stream);
                }
                while (1) {
                    unsigned char *v1_2;

                    if (*(int *)((char *)stream + 0x4c) == 0) {
                        v1_2 = *(unsigned char **)((char *)stream + 0x10);
                        if (v1_2 >= *(unsigned char **)((char *)stream + 0x18)) {
                            i = (unsigned int)__fgetc_unlocked(stream);
                            break;
                        }
                        *(unsigned char **)((char *)stream + 0x10) = &v1_2[1];
                        s1_1 = &s1_1[1];
                        *s1_1 = *v1_2;
                        s6_1 += 1;
                    } else {
                        i = (unsigned int)fgetc(stream);
                    }
                    if (i == 0xffffffffU) {
                        break;
                    }
                    s6_1 += 1;
                    s1_1 = &s1_1[1];
                }
            }

            str[s6_1] = 0;
            var_2c = str;
            if (strlen(str) >= 4) {
                json_parse_document(&var_30, str);
                json_tree_to_string(var_30, &var_2c);
            } else {
                var_30 = json_new_object();
                json_insert_child(var_30, json_init("Label", "Value"));
                json_tree_to_string(var_30, &var_2c);
                json_free_value(&var_30);
                json_parse_document(&var_30, var_2c);
            }
            fclose(stream);
        }
    }

    var_2c = json_format_string(var_2c);
    {
        FILE *stream_1 = fopen("/tmp/Settings.jz", "w");
        json_value *v0_17;
        json_value *s1_2;
        char *str1;
        char *str1_1;
        int result;

        fprintf(stream_1, "%s\n", var_2c);
        v0_17 = json_find_first_label(var_30, "entry");
        s1_2 = json_find_first_label(v0_17->child, "Label");
        while (1) {
            str1 = s1_2->text;
            if (strcmp(str1, " ") == 0) {
                break;
            }
            if (strcmp(str1, arg1) == 0) {
                break;
            }
            s1_2 = s1_2->next;
        }

        str1_1 = json_find_first_label(v0_17->child, str1)->child->text;
        if (strcmp(str1_1, "On") == 0) {
            printf("[System Wrapper]: %s is enabled!!!\n", str1);
            result = 0;
        } else if (strcmp(str1_1, "Off") == 0) {
            printf("[System Wrapper]: %s is disabled!!!\n", str1);
            result = 1;
        } else if (strcmp(str1_1, " ") == 0) {
            result = -1;
            puts("[System Wrapper]: Nothing is selected!!!");
        } else {
            result = -1;
            puts("[System Wrapper]: Please check your json file!!!");
        }

        fclose(stream_1);
        free(var_2c);
        json_free_value(&var_30);
        return result;
    }
}
