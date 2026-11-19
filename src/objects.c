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
#include "map.h"      /* MAP_TILES_PER_ROW, MAP_TILE_PX — геометрия атласа */
#include "inventory.h" /* INVENTORY_SIZE — предел размеров предмета        */
#include "objects.h"

/* ------------------------------------------------------------ хранилище */

static game_object_desc g_objects[OBJECTS_MAX];
static int              g_object_count = 0;

static character_desc g_characters[CHARACTERS_MAX];
static int            g_character_count = 0;

/* фракции: название (Windows-1251) и флаг управления игроком */
static char g_faction_names[CHAR_FACTION_COUNT][OBJECT_NAME_MAX];
static int  g_faction_player[CHAR_FACTION_COUNT];

/* фракции: здоровье и координаты тайлов 128x128 в img/hudimages.png
 * (в пикселях атласа, левый верхний угол; -1 — не заданы) */
#define FACTION_HEALTH_DEFAULT 100
static int g_faction_health[CHAR_FACTION_COUNT];
static int g_faction_hud_x[CHAR_FACTION_COUNT][CHAR_TILE_STATE_COUNT];
static int g_faction_hud_y[CHAR_FACTION_COUNT][CHAR_TILE_STATE_COUNT];

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

/* Линейные размеры предмета в инвентаре по умолчанию (в клетках):
 * оружие — 1x2 (легкое) и 1x3 (тяжелое — от 3 кг), боеприпас, аптечка
 * и поверап — 1x1; ключи inv_size_x/inv_size_y переопределяют. */
static void default_inv_size(const game_object_desc *o, int *sx, int *sy)
{
    if (o->cls == OBJ_CLASS_WEAPON) {
        if (o->weight_kg >= 3.0f) {
            *sx = OBJECT_INV_SIZE_HEAVY_X;
            *sy = OBJECT_INV_SIZE_HEAVY_Y;
        } else {
            *sx = OBJECT_INV_SIZE_LIGHT_X;
            *sy = OBJECT_INV_SIZE_LIGHT_Y;
        }
    } else {
        *sx = OBJECT_INV_SIZE_SMALL_X;
        *sy = OBJECT_INV_SIZE_SMALL_Y;
    }
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

        /* линейные размеры предмета в инвентаре (в клетках) */
        default_inv_size(o, &o->inventory_size_x, &o->inventory_size_y);
        o->inventory_size_x =
            config_get_int(cfg, sect, "inv_size_x", o->inventory_size_x);
        o->inventory_size_y =
            config_get_int(cfg, sect, "inv_size_y", o->inventory_size_y);
        if (o->inventory_size_x < 1)
            o->inventory_size_x = 1;
        if (o->inventory_size_y < 1)
            o->inventory_size_y = 1;
        if (o->inventory_size_x > INVENTORY_SIZE)
            o->inventory_size_x = INVENTORY_SIZE;
        if (o->inventory_size_y > INVENTORY_SIZE)
            o->inventory_size_y = INVENTORY_SIZE;

        /* картинка предмета в инвентаре: прямоугольник в атласе
         * img/hudimages.png (клетки сетки 32x32 — по умолчанию ряд 13,
         * колонка N; размер равен габаритам предмета в клетках); в
         * конфиге задается ключами inv_img_row/inv_img_col */
        int img_col = config_get_int(cfg, sect, "inv_img_col", g_object_count);
        int img_row = config_get_int(cfg, sect, "inv_img_row",
                                     OBJECT_INV_IMG_ROW);
        if (img_col < 1)
            img_col = 1;
        if (img_row < 1)
            img_row = 1;
        o->inv_img_x = (img_col - 1) * MAP_TILE_PX;
        o->inv_img_y = (img_row - 1) * MAP_TILE_PX;
        o->inv_img_w = o->inventory_size_x * MAP_TILE_PX;
        o->inv_img_h = o->inventory_size_y * MAP_TILE_PX;

        /* большой тайл предмета 128x128 в img/hudimages.png (конфиг —
         * hud_img_row/hud_img_col); по умолчанию тайла нет */
        o->hud_img_w = 0;
        o->hud_img_h = 0;
        if (config_get(cfg, sect, "hud_img_row", NULL) &&
            config_get(cfg, sect, "hud_img_col", NULL)) {
            int hud_col = config_get_int(cfg, sect, "hud_img_col", 1);
            int hud_row = config_get_int(cfg, sect, "hud_img_row", 1);
            if (hud_col < 1)
                hud_col = 1;
            if (hud_row < 1)
                hud_row = 1;
            o->hud_img_x = (hud_col - 1) * OBJECT_HUD_IMG_PX;
            o->hud_img_y = (hud_row - 1) * OBJECT_HUD_IMG_PX;
            o->hud_img_w = OBJECT_HUD_IMG_PX;
            o->hud_img_h = OBJECT_HUD_IMG_PX;
        }
    }
}

/* Координаты тайла фракции по умолчанию: тайлы 0..8 атласа — три
 * состояния (полное/поврежденный/мертв) трех фракций подряд.
 * Тайл номер t лежит в ряду t/8 (с 0), колонке t%8 (с 0). */
static void default_faction_hud(int faction, char_tile_state st,
                                int *x, int *y)
{
    int t = faction * CHAR_TILE_STATE_COUNT + (int)st;
    *x = (t % 8) * OBJECT_HUD_IMG_PX;
    *y = (t / 8) * OBJECT_HUD_IMG_PX;
}

/* Чтение пары ключей hud_img_row_<суффикс> / hud_img_col_<суффикс>
 * (1-базные ряд и колонка тайла 128x128); 1 — прочитаны, 0 — нет. */
static int read_faction_hud(const config *cfg, const char *sect,
                            const char *suffix, int *x, int *y)
{
    char key[40];
    snprintf(key, sizeof(key), "hud_img_row_%s", suffix);
    if (!config_get(cfg, sect, key, NULL))
        return 0;
    snprintf(key, sizeof(key), "hud_img_col_%s", suffix);
    if (!config_get(cfg, sect, key, NULL))
        return 0;

    snprintf(key, sizeof(key), "hud_img_row_%s", suffix);
    int row = config_get_int(cfg, sect, key, 1);
    snprintf(key, sizeof(key), "hud_img_col_%s", suffix);
    int col = config_get_int(cfg, sect, key, 1);
    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;
    *x = (col - 1) * OBJECT_HUD_IMG_PX;
    *y = (row - 1) * OBJECT_HUD_IMG_PX;
    return 1;
}

static const char *tile_state_suffix(char_tile_state st)
{
    switch (st) {
    case CHAR_TILE_FULL:    return "full";
    case CHAR_TILE_DAMAGED: return "damaged";
    default:                return "dead";
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

        /* здоровье и тайлы персонажа 128x128 в img/hudimages.png */
        g_faction_health[n] = config_get_int(cfg, sect, "health",
                                             FACTION_HEALTH_DEFAULT);
        if (g_faction_health[n] < 1)
            g_faction_health[n] = 1;
        for (int s = 0; s < CHAR_TILE_STATE_COUNT; s++) {
            int x, y;
            default_faction_hud(n, (char_tile_state)s, &x, &y);
            read_faction_hud(cfg, sect, tile_state_suffix((char_tile_state)s),
                             &x, &y);
            g_faction_hud_x[n][s] = x;
            g_faction_hud_y[n][s] = y;
        }
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

        /* дальность видимости: радиус раскрытия "тумана войны"; по
         * умолчанию 12 элементов карты (CHAR_DEFAULT_VIS_RANGE) */
        c->vis_range = config_get_int(cfg, sect, "vis_range",
                                      CHAR_DEFAULT_VIS_RANGE);
        if (c->vis_range < 0)
            c->vis_range = CHAR_DEFAULT_VIS_RANGE;
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

int objects_faction_health(char_faction faction)
{
    if (faction < 0 || faction >= CHAR_FACTION_COUNT)
        return FACTION_HEALTH_DEFAULT;
    return g_faction_health[faction];
}

int objects_faction_hud_img(char_faction faction, char_tile_state st,
                            int *x, int *y)
{
    if (faction < 0 || faction >= CHAR_FACTION_COUNT ||
        st < 0 || st >= CHAR_TILE_STATE_COUNT)
        return -1;
    if (x)
        *x = g_faction_hud_x[faction][st];
    if (y)
        *y = g_faction_hud_y[faction][st];
    return 0;
}
