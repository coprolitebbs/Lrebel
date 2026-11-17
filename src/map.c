/*
 * map.c — карта игры: реестр типов объектов, слои, файл карты, камера,
 * преобразования координат и отрисовка.
 *
 * Верхний уровень: работает только через portable_posix.h (изображения,
 * файлы, ввод, примитивы) — никакой графики нижнего уровня здесь нет.
 *
 * Файл карты (.map) — простой бинарный формат со сжатием длинных серий
 * (RLE), все числа записываются в порядке младшего байта первым:
 *   4 байта  магия 'L','R','M','1'
 *   u32      ширина карты, u32 высота (в элементах)
 *   4 слоя:  u32 число серий, далее серии { u32 длина, u16 номер тайла }
 *   u32      число объектов
 *   объекты: { u16 тип, u16 направление, f32 x, f32 y }
 *   блоки:   u32 число серий, далее серии { u32 длина, u8 флаг }
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "readconfig.h"
#include "map.h"

/* ------------------------------------------------------------ реестр */

map_char_type map_char_types[OBJ_TYPE_CHAR_COUNT] = {
    { 1, 1, 1 }, /* первый персонаж управляется игроком */
    { 2, 1, 0 },
    { 3, 1, 0 }  /* тайлы анимации начинаются с колонки 1 (title_col=1) */
};

static int   g_def_w = MAP_MAX_SIZE, g_def_h = MAP_MAX_SIZE;
static int   g_vis = 32;
static float g_camera_speed = 8.0f;
static int   g_pickable = 15;
static int   g_assets = 30;

static int bool_from_str(const char *s)
{
    return s && (strcmp(s, "true") == 0 || strcmp(s, "1") == 0 ||
                 strcmp(s, "yes") == 0 || strcmp(s, "on") == 0);
}

int map_registry_init(const char *editor_cfg_path)
{
    /* значения по умолчанию остаются, если файла нет или параметра нет */
    config *cfg = config_load(editor_cfg_path);
    if (!cfg) {
        fprintf(stderr, "map: '%s' не найден — используются значения "
                        "по умолчанию\n", editor_cfg_path);
        return 0;
    }

    g_def_w = config_get_int(cfg, "editor", "map_width",  MAP_MAX_SIZE);
    g_def_h = config_get_int(cfg, "editor", "map_height", MAP_MAX_SIZE);
    if (g_def_w < 1)   g_def_w = 1;
    if (g_def_h < 1)   g_def_h = 1;
    if (g_def_w > MAP_MAX_SIZE) g_def_w = MAP_MAX_SIZE;
    if (g_def_h > MAP_MAX_SIZE) g_def_h = MAP_MAX_SIZE;

    g_vis = config_get_int(cfg, "editor", "visible_elements", 32);
    if (g_vis < 8)  g_vis = 8;
    if (g_vis > 128) g_vis = 128;

    g_assets = config_get_int(cfg, "editor", "asset_count", 30);
    if (g_assets < 0) g_assets = 0;

    g_pickable = config_get_int(cfg, "objects", "pickable_count", 15);
    if (g_pickable < 0) g_pickable = 0;

    const char *speed = config_get(cfg, "editor", "camera_speed", NULL);
    if (speed) {
        float v = (float)strtod(speed, NULL);
        if (v > 0.0f)
            g_camera_speed = v;
    }

    for (int i = 0; i < OBJ_TYPE_CHAR_COUNT; i++) {
        char sect[16];
        snprintf(sect, sizeof(sect), "char%d", i + 1);
        map_char_types[i].tile_row =
            config_get_int(cfg, sect, "tile_row", i + 1);
        if (map_char_types[i].tile_row < 1)
            map_char_types[i].tile_row = i + 1;
        map_char_types[i].title_col =
            config_get_int(cfg, sect, "title_col", 1);
        if (map_char_types[i].title_col < 1)
            map_char_types[i].title_col = 1;
        map_char_types[i].player =
            bool_from_str(config_get(cfg, sect, "player",
                                     i == 0 ? "true" : "false"));
    }

    config_free(cfg);
    return 0;
}

int map_def_width(void)      { return g_def_w; }
int map_def_height(void)     { return g_def_h; }
int map_vis_elements(void)   { return g_vis; }
float map_camera_speed(void) { return g_camera_speed; }
int map_pickable_count(void) { return g_pickable; }
int map_asset_count(void)    { return g_assets; }

int map_object_type_count(void)
{
    return OBJ_TYPE_CHAR_COUNT + g_pickable;
}

/* Первый ассет — колонка 1 строки 22 тайлового листа. */
#define MAP_ASSET_ROW 22

int map_object_tile(int type, int dir)
{
    if (type >= OBJ_TYPE_CHAR_FIRST &&
        type < OBJ_TYPE_CHAR_FIRST + OBJ_TYPE_CHAR_COUNT) {
        const map_char_type *t = &map_char_types[type];
        int d = (dir >= 0 && dir < OBJ_DIR_COUNT) ? dir : OBJ_DIR_DOWN;
        return MAP_TILE_ID(t->tile_row, t->title_col + d);
    }
    if (type >= OBJ_TYPE_WEAPON_FIRST &&
        type < OBJ_TYPE_WEAPON_FIRST + g_pickable)
        return MAP_TILE_ID(4, type - OBJ_TYPE_WEAPON_FIRST + 1);
    return -1;
}

int map_asset_tile(int index)
{
    if (index < 0 || index >= g_assets)
        return 0;
    return MAP_TILE_ID(MAP_ASSET_ROW, 1) + index;
}

int map_surface_first_tile(void)
{
    return MAP_TILE_ID(MAP_SURFACE_ROW, 1);
}

int map_walls_last_tile(void)
{
    return MAP_TILE_ID(MAP_WALLS_LAST_ROW, MAP_TILES_PER_ROW);
}

/* ------------------------------------------------------- карта: память */

int map_init(game_map *m, int w, int h)
{
    memset(m, 0, sizeof(*m));
    if (w < 1 || h < 1 || w > MAP_MAX_SIZE || h > MAP_MAX_SIZE)
        return -1;
    for (int l = 0; l < MAP_LAYERS; l++) {
        m->layers[l] = (unsigned short *)
            calloc((size_t)w * (size_t)h, sizeof(unsigned short));
        if (!m->layers[l]) {
            map_free(m);
            return -1;
        }
    }
    m->blocks = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!m->blocks) {
        map_free(m);
        return -1;
    }
    m->w = w;
    m->h = h;
    m->object_count = 0;
    m->modified = 0;
    return 0;
}

void map_free(game_map *m)
{
    if (!m)
        return;
    for (int l = 0; l < MAP_LAYERS; l++) {
        free(m->layers[l]);
        m->layers[l] = NULL;
    }
    free(m->blocks);
    m->blocks = NULL;
    m->object_count = 0;
    m->modified = 0;
}

/* ------------------------------------------------------- файл: примитивы */

static int w8(plat_file *f, unsigned v)
{
    unsigned char b = (unsigned char)(v & 0xFF);
    return plat_file_write(f, &b, 1) == 1 ? 0 : -1;
}

static int w16(plat_file *f, unsigned v)
{
    unsigned char b[2] = { (unsigned char)(v & 0xFF),
                           (unsigned char)((v >> 8) & 0xFF) };
    return plat_file_write(f, b, 2) == 2 ? 0 : -1;
}

static int w32(plat_file *f, unsigned long v)
{
    unsigned char b[4] = { (unsigned char)(v & 0xFF),
                           (unsigned char)((v >> 8) & 0xFF),
                           (unsigned char)((v >> 16) & 0xFF),
                           (unsigned char)((v >> 24) & 0xFF) };
    return plat_file_write(f, b, 4) == 4 ? 0 : -1;
}

static int wf32(plat_file *f, float v)
{
    /* биты float записываются как u32 (поразрядно, без преобразований) */
    union { float f; unsigned long u; } conv;
    conv.f = v;
    return w32(f, conv.u & 0xFFFFFFFFul);
}

static int r8(plat_file *f, unsigned *v)
{
    unsigned char b = 0;
    if (plat_file_read(f, &b, 1) != 1)
        return -1;
    *v = b;
    return 0;
}

static int r16(plat_file *f, unsigned *v)
{
    unsigned char b[2] = { 0, 0 };
    if (plat_file_read(f, b, 2) != 2)
        return -1;
    *v = (unsigned)b[0] | ((unsigned)b[1] << 8);
    return 0;
}

static int r32(plat_file *f, unsigned long *v)
{
    unsigned char b[4] = { 0, 0, 0, 0 };
    if (plat_file_read(f, b, 4) != 4)
        return -1;
    *v = (unsigned long)b[0] | ((unsigned long)b[1] << 8) |
         ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
    return 0;
}

static int rf32(plat_file *f, float *v)
{
    unsigned long u = 0;
    union { float f; unsigned long u; } conv;
    if (r32(f, &u) != 0)
        return -1;
    conv.u = u;
    *v = conv.f;
    return 0;
}

/* ------------------------------------------------------- файл: серии RLE */

static int rle16_write(plat_file *f, const unsigned short *data, int n)
{
    unsigned long runs = 0;
    for (int i = 0; i < n; ) {
        int j = i + 1;
        while (j < n && data[j] == data[i])
            j++;
        runs++;
        i = j;
    }
    if (w32(f, runs) != 0)
        return -1;
    for (int i = 0; i < n; ) {
        int j = i + 1;
        while (j < n && data[j] == data[i])
            j++;
        if (w32(f, (unsigned long)(j - i)) != 0 || w16(f, data[i]) != 0)
            return -1;
        i = j;
    }
    return 0;
}

static int rle16_read(plat_file *f, unsigned short *data, int n)
{
    unsigned long runs = 0;
    if (r32(f, &runs) != 0)
        return -1;
    int filled = 0;
    for (unsigned long r = 0; r < runs; r++) {
        unsigned long len = 0;
        unsigned val = 0;
        if (r32(f, &len) != 0 || r16(f, &val) != 0)
            return -1;
        if (len == 0 || filled + (int)len > n)
            return -1;
        for (unsigned long k = 0; k < len; k++)
            data[filled++] = (unsigned short)val;
    }
    while (filled < n)   /* файл короче карты — остаток пустой */
        data[filled++] = 0;
    return 0;
}

static int rle8_write(plat_file *f, const unsigned char *data, int n)
{
    unsigned long runs = 0;
    for (int i = 0; i < n; ) {
        int j = i + 1;
        while (j < n && data[j] == data[i])
            j++;
        runs++;
        i = j;
    }
    if (w32(f, runs) != 0)
        return -1;
    for (int i = 0; i < n; ) {
        int j = i + 1;
        while (j < n && data[j] == data[i])
            j++;
        if (w32(f, (unsigned long)(j - i)) != 0 || w8(f, data[i]) != 0)
            return -1;
        i = j;
    }
    return 0;
}

static int rle8_read(plat_file *f, unsigned char *data, int n)
{
    unsigned long runs = 0;
    if (r32(f, &runs) != 0)
        return -1;
    int filled = 0;
    for (unsigned long r = 0; r < runs; r++) {
        unsigned long len = 0;
        unsigned val = 0;
        if (r32(f, &len) != 0 || r8(f, &val) != 0)
            return -1;
        if (len == 0 || filled + (int)len > n)
            return -1;
        for (unsigned long k = 0; k < len; k++)
            data[filled++] = (unsigned char)val;
    }
    while (filled < n)
        data[filled++] = 0;
    return 0;
}

/* ------------------------------------------------------- файл: карта */

int map_load(game_map *m, const char *path)
{
    if (!m || !path)
        return -1;
    map_free(m);
    memset(m, 0, sizeof(*m));

    plat_file *f = plat_file_open_read(path);
    if (!f) {
        fprintf(stderr, "map: карта '%s' не найдена\n", path);
        return -1;
    }
    unsigned magic[4] = { 0, 0, 0, 0 };
    unsigned long w = 0, h = 0;
    int ok = (r8(f, &magic[0]) == 0 && r8(f, &magic[1]) == 0 &&
              r8(f, &magic[2]) == 0 && r8(f, &magic[3]) == 0 &&
              magic[0] == 'L' && magic[1] == 'R' && magic[2] == 'M' &&
              magic[3] == '1' &&
              r32(f, &w) == 0 && r32(f, &h) == 0 &&
              w >= 1 && h >= 1 && w <= MAP_MAX_SIZE && h <= MAP_MAX_SIZE);
    if (ok)
        ok = map_init(m, (int)w, (int)h) == 0;

    for (int l = 0; ok && l < MAP_LAYERS; l++)
        ok = rle16_read(f, m->layers[l], m->w * m->h) == 0;

    unsigned long count = 0;
    if (ok)
        ok = r32(f, &count) == 0 && count <= MAP_OBJECTS_MAX;
    for (unsigned long i = 0; ok && i < count; i++) {
        unsigned type = 0, dir = 0;
        float x = 0.0f, y = 0.0f;
        ok = r16(f, &type) == 0 && r16(f, &dir) == 0 &&
             rf32(f, &x) == 0 && rf32(f, &y) == 0;
        if (ok) {
            map_object *o = &m->objects[m->object_count++];
            o->type = (int)type;
            o->dir = (int)dir;
            o->x = x;
            o->y = y;
        }
    }

    if (ok)
        ok = rle8_read(f, m->blocks, m->w * m->h) == 0;

    plat_file_close(f);
    if (!ok) {
        fprintf(stderr, "map: файл карты '%s' повреждён\n", path);
        map_free(m);
        memset(m, 0, sizeof(*m));
        return -1;
    }
    m->modified = 0;
    return 0;
}

int map_save(const game_map *m, const char *path)
{
    if (!m || !path || !*path || m->w < 1 || m->h < 1)
        return -1;

    /* папка maps создаётся при первом сохранении (файл может ещё не
     * существовать — он будет создан) */
    if (strncmp(path, "maps/", 5) == 0 && strchr(path + 5, '/') == NULL)
        plat_mkdir("maps");

    plat_file *f = plat_file_open_write(path);
    if (!f) {
        fprintf(stderr, "map_save: не удалось открыть '%s' для записи\n",
                path);
        return -1;
    }

    int ok = w8(f, 'L') == 0 && w8(f, 'R') == 0 &&
             w8(f, 'M') == 0 && w8(f, '1') == 0 &&
             w32(f, (unsigned long)m->w) == 0 &&
             w32(f, (unsigned long)m->h) == 0;
    for (int l = 0; ok && l < MAP_LAYERS; l++)
        ok = rle16_write(f, m->layers[l], m->w * m->h) == 0;
    if (ok)
        ok = w32(f, (unsigned long)m->object_count) == 0;
    for (int i = 0; ok && i < m->object_count; i++) {
        const map_object *o = &m->objects[i];
        ok = w16(f, (unsigned)o->type) == 0 &&
             w16(f, (unsigned)o->dir) == 0 &&
             wf32(f, o->x) == 0 && wf32(f, o->y) == 0;
    }
    if (ok)
        ok = rle8_write(f, m->blocks, m->w * m->h) == 0;

    plat_file_close(f);
    if (!ok) {
        fprintf(stderr, "map_save: ошибка записи '%s'\n", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------- объекты */

int map_cell_occupied(const game_map *m, int cx, int cy)
{
    for (int i = 0; i < m->object_count; i++) {
        const map_object *o = &m->objects[i];
        if ((int)floorf(o->x) == cx && (int)floorf(o->y) == cy)
            return 1;
    }
    return 0;
}

int map_object_add(game_map *m, int type, float x, float y, int dir)
{
    if (m->object_count >= MAP_OBJECTS_MAX)
        return -1;
    int cx = (int)floorf(x), cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h)
        return -1;
    if (map_cell_occupied(m, cx, cy))
        return -1; /* на имеющийся объект другой не ставится */
    map_object *o = &m->objects[m->object_count++];
    o->type = type;
    o->x = x;
    o->y = y;
    o->dir = dir;
    return m->object_count - 1;
}

int map_object_remove_at(game_map *m, int cx, int cy)
{
    for (int i = 0; i < m->object_count; i++) {
        map_object *o = &m->objects[i];
        if ((int)floorf(o->x) == cx && (int)floorf(o->y) == cy) {
            memmove(&m->objects[i], &m->objects[i + 1],
                    (size_t)(m->object_count - i - 1) * sizeof(map_object));
            m->object_count--;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------- камера */

void camera_init(camera *c, int vis, float speed, float max_x, float max_y)
{
    c->x = max_x > 0.0f ? max_x / 2.0f : 0.0f;
    c->y = max_y > 0.0f ? max_y / 2.0f : 0.0f;
    c->speed = speed > 0.0f ? speed : 8.0f;
    c->vis = vis > 0 ? vis : 32;
    c->min_x = 0.0f;
    c->min_y = 0.0f;
    c->max_x = max_x;
    c->max_y = max_y;
}

void camera_clamp(camera *c)
{
    if (c->x < c->min_x) c->x = c->min_x;
    if (c->y < c->min_y) c->y = c->min_y;
    if (c->x > c->max_x) c->x = c->max_x;
    if (c->y > c->max_y) c->y = c->max_y;
}

void camera_update(camera *c, float dt)
{
    float step = c->speed * dt;
    if (step <= 0.0f)
        return;
    if (plat_key_held('w'))
        c->y -= step;
    if (plat_key_held('s') && !plat_key_ctrl())
        c->y += step; /* без Ctrl — чтобы не мешать Ctrl+S */
    if (plat_key_held('a'))
        c->x -= step;
    if (plat_key_held('d'))
        c->x += step;
    camera_clamp(c);
}

/* --------------------------------------------- координаты и отрисовка */

float map_tile_px(const camera *c)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    int m = sw < sh ? sw : sh;
    if (m <= 0 || c->vis <= 0)
        return 1.0f;
    return (float)m / (float)c->vis;
}

void map_world_to_screen(const camera *c, float wx, float wy,
                         int *sx, int *sy)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    float tpx = map_tile_px(c);
    if (sx) *sx = (int)((wx - c->x) * tpx + (float)sw / 2.0f);
    if (sy) *sy = (int)((wy - c->y) * tpx + (float)sh / 2.0f);
}

void map_screen_to_world(const camera *c, int sx, int sy,
                         float *wx, float *wy)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    float tpx = map_tile_px(c);
    if (wx) *wx = ((float)sx - (float)sw / 2.0f) / tpx + c->x;
    if (wy) *wy = ((float)sy - (float)sh / 2.0f) / tpx + c->y;
}

void map_draw(const game_map *m, const camera *c, const plat_image *tiles,
              int show_blocks)
{
    if (!m || !c || !tiles || m->w < 1 || m->h < 1)
        return;

    int sw = plat_screen_width();
    int sh = plat_screen_height();
    float tpx = map_tile_px(c);
    int tpi = tpx >= 1.0f ? (int)tpx : 1;
    float cx = (float)sw / 2.0f;
    float cy = (float)sh / 2.0f;

    /* видимые элементы: по vis/2 во все стороны от камеры (+ запас) */
    int half = c->vis / 2 + 1;
    int x0 = (int)floorf(c->x) - half;
    int x1 = (int)floorf(c->x) + half;
    int y0 = (int)floorf(c->y) - half;
    int y1 = (int)floorf(c->y) + half;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= m->w) x1 = m->w - 1;
    if (y1 >= m->h) y1 = m->h - 1;

    /* слои от нижнего к верхнему */
    for (int row = y0; row <= y1; row++) {
        int sy = (int)(((float)row - c->y) * tpx + cy);
        for (int col = x0; col <= x1; col++) {
            int sx = (int)(((float)col - c->x) * tpx + cx);
            int cell = row * m->w + col;
            for (int layer = 0; layer < MAP_LAYERS; layer++) {
                unsigned short id = m->layers[layer][cell];
                if (!id)
                    continue;
                plat_draw_image(tiles,
                                (int)(id % MAP_TILES_PER_ROW) * MAP_TILE_PX,
                                (int)(id / MAP_TILES_PER_ROW) * MAP_TILE_PX,
                                MAP_TILE_PX, MAP_TILE_PX,
                                sx, sy, tpi, tpi);
            }
        }
    }

    /* объекты — всегда поверх всех слоёв карты */
    for (int i = 0; i < m->object_count; i++) {
        const map_object *o = &m->objects[i];
        int tile = map_object_tile(o->type, o->dir);
        if (tile < 0)
            continue;
        int sx = (int)((o->x - c->x) * tpx + cx);
        int sy = (int)((o->y - c->y) * tpx + cy);
        if (sx + tpi < 0 || sy + tpi < 0 || sx >= sw || sy >= sh)
            continue;
        plat_draw_image(tiles,
                        (tile % MAP_TILES_PER_ROW) * MAP_TILE_PX,
                        (tile / MAP_TILES_PER_ROW) * MAP_TILE_PX,
                        MAP_TILE_PX, MAP_TILE_PX,
                        sx, sy, tpi, tpi);
    }

    /* невидимые блоки — только в редакторе: красная рамка поверх тайла */
    if (show_blocks) {
        for (int row = y0; row <= y1; row++) {
            for (int col = x0; col <= x1; col++) {
                if (!m->blocks[row * m->w + col])
                    continue;
                int sx = (int)(((float)col - c->x) * tpx + cx);
                int sy = (int)(((float)row - c->y) * tpx + cy);
                plat_draw_rect_frame(sx, sy, tpi, tpi, 255, 48, 48);
            }
        }
    }
}
