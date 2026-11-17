/*
 * objects.c — загрузка описаний игровых объектов и персонажей из
 * config/objects.cfg (формат — как в readconfig.h, кодировка UTF-8).
 *
 * Строки перекодируются в Windows-1251 сразу при загрузке: шрифт (font.h)
 * и весь интерфейс работают с однобайтовыми строками Windows-1251.
 * Хранилище статическое — загрузка выполняется один раз при старте игры.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "readconfig.h"
#include "utf8_win1251.h"
#include "objects.h"

/* ------------------------------------------------------------ хранилище */

static game_object_desc g_objects[OBJECTS_MAX];
static int              g_object_count = 0;

static character_desc g_characters[CHARACTERS_MAX];
static int            g_character_count = 0;

/* фракции: название (Windows-1251) и флаг управления игроком */
static char g_faction_names[CHAR_FACTION_COUNT][OBJECT_NAME_MAX];
static int  g_faction_player[CHAR_FACTION_COUNT];

/* ------------------------------------------------------------ утилиты */

/* UTF-8 -> Windows-1251 с усечением до размера буфера. */
static void copy_win1251(char *dst, size_t dst_size, const char *utf8)
{
    if (!dst || dst_size == 0)
        return;
    if (utf8_to_win1251(utf8 ? utf8 : "", dst, dst_size) < 0)
        dst[dst_size - 1] = '\0'; /* усечено — строка уже завершена */
}

static object_class class_from_str(const char *s)
{
    if (!s || !*s)
        return OBJ_CLASS_WEAPON;
    if (strcmp(s, "ammo") == 0)
        return OBJ_CLASS_AMMO;
    if (strcmp(s, "medkit") == 0)
        return OBJ_CLASS_MEDKIT;
    if (strcmp(s, "powerup") == 0)
        return OBJ_CLASS_POWERUP;
    return OBJ_CLASS_WEAPON;
}

/* Максимальное количество по умолчанию: оружие/аптечка/поверап — 1,
 * боеприпас — 20 (тяжелое; в конфиге переопределяется). */
static int default_max_count(object_class cls)
{
    return cls == OBJ_CLASS_AMMO ? 20 : 1;
}

static int sex_from_str(const char *s)
{
    if (s && (strcmp(s, "female") == 0 || strcmp(s, "1") == 0))
        return CHAR_SEX_FEMALE;
    return CHAR_SEX_MALE;
}

static void init_faction_defaults(void)
{
    static const char *names[CHAR_FACTION_COUNT] = {
        "Подразделение \"Спецназ-9\"",
        "Повстанцы и террористы",
        "Киборги и роботы"
    };
    for (int i = 0; i < CHAR_FACTION_COUNT; i++) {
        copy_win1251(g_faction_names[i], OBJECT_NAME_MAX, names[i]);
        g_faction_player[i] = (i == CHAR_FACTION_SPETSNAZ);
    }
}

/* ------------------------------------------------------------ загрузка */

static void load_objects(const config *cfg)
{
    g_object_count = 0;
    for (int n = 0; n < OBJECTS_MAX; n++) {
        char sect[24];
        snprintf(sect, sizeof(sect), "object%d", n + 1);
        const char *name = config_get(cfg, sect, "name", NULL);
        if (!name)
            continue; /* секции нет — объект не описан, чтение до первого */
        if (g_object_count != n)
            fprintf(stderr, "objects: пропуск секции {%s} — секции objectN "
                            "должны идти без пропусков\n", sect);

        game_object_desc *o = &g_objects[g_object_count++];
        memset(o, 0, sizeof(*o));

        copy_win1251(o->name, sizeof(o->name), name);
        for (int d = 0; d < OBJECT_DESC_LINES; d++) {
            char key[16];
            snprintf(key, sizeof(key), "desc%d", d + 1);
            copy_win1251(o->desc[d], sizeof(o->desc[d]),
                         config_get(cfg, sect, key, ""));
        }

        o->cls = class_from_str(config_get(cfg, sect, "type", "weapon"));
        o->max_count =
            config_get_int(cfg, sect, "max_count", default_max_count(o->cls));
        if (o->max_count < 1)
            o->max_count = 1;
        o->count = config_get_int(cfg, sect, "count", o->max_count);
        if (o->count < 0)
            o->count = 0;
        if (o->count > o->max_count)
            o->count = o->max_count; /* реальное не превышает максимальное */

        o->damage = config_get_int(cfg, sect, "damage", 0);
        if (o->damage < 0)
            o->damage = 0;
        o->reload_turns = config_get_int(cfg, sect, "reload_turns", 0);
        if (o->reload_turns < 0)
            o->reload_turns = 0;
        o->magazine_size = config_get_int(cfg, sect, "magazine_size", 0);
        if (o->magazine_size < 0)
            o->magazine_size = 0;
        o->weight_kg = config_get_float(cfg, sect, "weight", 0.0f);
        if (o->weight_kg < 0.0f)
            o->weight_kg = 0.0f;
    }
}

static void load_factions(const config *cfg)
{
    for (int n = 0; n < CHAR_FACTION_COUNT; n++) {
        char sect[24];
        snprintf(sect, sizeof(sect), "char_type%d", n + 1);
        copy_win1251(g_faction_names[n], OBJECT_NAME_MAX,
                     config_get(cfg, sect, "name", g_faction_names[n]));
        const char *control = config_get(cfg, sect, "control", NULL);
        if (control)
            g_faction_player[n] = (strcmp(control, "player") == 0);
    }
}

static void load_characters(const config *cfg)
{
    g_character_count = 0;
    for (int n = 0; n < CHARACTERS_MAX; n++) {
        char sect[32];
        snprintf(sect, sizeof(sect), "character%d", n + 1);
        const char *name = config_get(cfg, sect, "name", NULL);
        if (!name)
            continue;

        character_desc *c = &g_characters[g_character_count++];
        memset(c, 0, sizeof(*c));

        copy_win1251(c->name, sizeof(c->name), name);
        c->age = config_get_int(cfg, sect, "age", 0);
        if (c->age < 0)
            c->age = 0;
        c->sex = sex_from_str(config_get(cfg, sect, "sex", "male"));

        int faction = config_get_int(cfg, sect, "faction", 1) - 1;
        if (faction < 0)
            faction = 0;
        if (faction >= CHAR_FACTION_COUNT)
            faction = CHAR_FACTION_COUNT - 1;
        c->faction = (char_faction)faction;

        const char *control = config_get(cfg, sect, "control", NULL);
        /* по умолчанию — как у фракции */
        c->is_player = control
            ? (strcmp(control, "player") == 0)
            : objects_faction_is_player(c->faction);

        c->steps_per_turn = config_get_int(cfg, sect, "steps", 0);
        if (c->steps_per_turn < 0)
            c->steps_per_turn = 0;
    }
}

int objects_load(const char *cfg_path)
{
    init_faction_defaults();
    g_object_count = 0;
    g_character_count = 0;

    config *cfg = config_load(cfg_path);
    if (!cfg) {
        fprintf(stderr, "objects: '%s' не найден — описания объектов "
                        "не загружены\n", cfg_path);
        return -1;
    }

    load_objects(cfg);
    load_factions(cfg);
    load_characters(cfg);
    config_free(cfg);

    printf("objects: загружено описаний: %d объектов, %d персонажей "
           "(%s)\n", g_object_count, g_character_count, cfg_path);
    return 0;
}

/* ------------------------------------------------------------ доступ */

int objects_desc_count(void)
{
    return g_object_count;
}

const game_object_desc *objects_desc(int index)
{
    if (index < 0 || index >= g_object_count)
        return NULL;
    return &g_objects[index];
}

int objects_character_count(void)
{
    return g_character_count;
}

const character_desc *objects_character(int index)
{
    if (index < 0 || index >= g_character_count)
        return NULL;
    return &g_characters[index];
}

const char *objects_faction_name(char_faction faction)
{
    if (faction < 0 || faction >= CHAR_FACTION_COUNT)
        return "?";
    return g_faction_names[faction];
}

int objects_faction_is_player(char_faction faction)
{
    if (faction < 0 || faction >= CHAR_FACTION_COUNT)
        return 0;
    return g_faction_player[faction];
}
