/*
 * readconfig.c — разбор конфигурационных файлов {section}-ini (UTF-8).
 * Файловый доступ — только через платформенный уровень (plat_file_*).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "readconfig.h"
#include "portable_posix.h"

/* ------------------------------------------------------------ структура */

typedef struct cfg_value {
    char *key;
    char *value;
    struct cfg_value *next;
} cfg_value;

typedef struct cfg_section {
    char *name;
    cfg_value *values;
    struct cfg_section *next;
} cfg_section;

struct config {
    cfg_section *sections;
};

/* ------------------------------------------------------------ утилиты */

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

/* Отбрасывает пробелы/табуляции/переводы строк по краям строки (in place). */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = '\0';
    return s;
}

static cfg_section *find_section(config *cfg, const char *name)
{
    for (cfg_section *s = cfg->sections; s; s = s->next)
        if (strcmp(s->name, name) == 0)
            return s;
    return NULL;
}

static cfg_section *add_section(config *cfg, const char *name)
{
    cfg_section *s = find_section(cfg, name);
    if (s)
        return s; /* повторное открытие секции продолжает накапливать в неё */
    s = (cfg_section *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->name = xstrdup(name);
    if (!s->name) {
        free(s);
        return NULL;
    }
    s->next = cfg->sections;
    cfg->sections = s;
    return s;
}

/* Последнее вхождение ключа в секции побеждает. */
static void set_value(cfg_section *s, const char *key, const char *value)
{
    for (cfg_value *v = s->values; v; v = v->next) {
        if (strcmp(v->key, key) == 0) {
            char *nv = xstrdup(value);
            if (nv) {
                free(v->value);
                v->value = nv;
            }
            return;
        }
    }
    cfg_value *v = (cfg_value *)calloc(1, sizeof(*v));
    if (!v)
        return;
    v->key = xstrdup(key);
    v->value = xstrdup(value);
    if (!v->key || !v->value) {
        free(v->key);
        free(v->value);
        free(v);
        return;
    }
    v->next = s->values;
    s->values = v;
}

static const cfg_value *get_value(const config *cfg,
                                  const char *section, const char *key)
{
    if (!cfg)
        return NULL;
    const cfg_section *s = find_section((config *)cfg, section);
    if (!s)
        return NULL;
    for (const cfg_value *v = s->values; v; v = v->next)
        if (strcmp(v->key, key) == 0)
            return v;
    return NULL;
}

/* ------------------------------------------------------------ разбор */

config *config_load(const char *path)
{
    if (!path)
        return NULL;

    plat_file *f = plat_file_open_read(path);
    if (!f)
        return NULL;
    long fsize = plat_file_size(f);
    if (fsize < 0) {
        plat_file_close(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) {
        plat_file_close(f);
        return NULL;
    }
    size_t got = plat_file_read(f, buf, (size_t)fsize);
    plat_file_close(f);
    buf[got] = '\0';

    config *cfg = (config *)calloc(1, sizeof(*cfg));
    if (!cfg) {
        free(buf);
        return NULL;
    }

    /* секция по умолчанию — для строк до первого {section} */
    cfg_section *cur = add_section(cfg, "");
    if (!cur) {
        config_free(cfg);
        free(buf);
        return NULL;
    }

    char *p = buf;
    while (p && *p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = '\0';
            p = nl + 1;
        } else {
            p = NULL;
        }

        char *s = trim(line);
        if (*s == '\0' || *s == '#' || *s == ';')
            continue; /* пустая строка или комментарий */

        if (*s == '{') {
            char *close = strchr(s, '}');
            if (!close) {
                fprintf(stderr, "readconfig: '%s': секция без '}': %s\n", path, s);
                continue;
            }
            *close = '\0';
            cur = add_section(cfg, trim(s + 1));
            if (!cur) {
                cur = add_section(cfg, "");
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "readconfig: '%s': строка без '=' пропущена: '%s'\n",
                    path, s);
            continue;
        }
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);
        if (*key == '\0') {
            fprintf(stderr, "readconfig: '%s': параметр с пустым именем\n", path);
            continue;
        }
        set_value(cur, key, value);
    }

    free(buf);
    return cfg;
}

void config_free(config *cfg)
{
    if (!cfg)
        return;
    cfg_section *s = cfg->sections;
    while (s) {
        cfg_section *sn = s->next;
        cfg_value *v = s->values;
        while (v) {
            cfg_value *vn = v->next;
            free(v->key);
            free(v->value);
            free(v);
            v = vn;
        }
        free(s->name);
        free(s);
        s = sn;
    }
    free(cfg);
}

/* ------------------------------------------------------------ доступ */

const char *config_get(const config *cfg,
                       const char *section, const char *key,
                       const char *default_value)
{
    const cfg_value *v = get_value(cfg, section, key);
    return v ? v->value : default_value;
}

int config_get_int(const config *cfg,
                   const char *section, const char *key,
                   int default_value)
{
    const cfg_value *v = get_value(cfg, section, key);
    if (!v)
        return default_value;
    char *end = NULL;
    long val = strtol(v->value, &end, 10);
    if (end == v->value)
        return default_value;
    return (int)val;
}

float config_get_float(const config *cfg,
                       const char *section, const char *key,
                       float default_value)
{
    const cfg_value *v = get_value(cfg, section, key);
    if (!v)
        return default_value;
    char *end = NULL;
    float val = strtof(v->value, &end);
    if (end == v->value)
        return default_value;
    return val;
}
