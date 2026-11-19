/*
 * game.c — игровой режим: см. game.h.
 *
 * Отрисовка — через portable_posix.h (изображения, примитивы, мигающие
 * рамки), шрифт (font.h), типовые окна и кнопки (ui.h); карта (map.h),
 * описания объектов и персонажей (objects.h). Надписи хранятся в
 * Windows-1251 и собираются один раз при game_init.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "portable_posix.h"
#include "utf8_win1251.h"
#include "font.h"
#include "ui.h"
#include "objects.h"
#include "map.h"
#include "game.h"

#define GAME_CFG        "config/editor.cfg" /* общий реестр с редактором  */
#define GTILES_PATH     "img/gtiles.png"
#define HUDIMG_PATH     "img/hudimages.png" /* тайлы HUD/персонажей        */
#define MAP_DIR         "maps"

#define GAME_KEY_ESC    27  /* ASCII-код клавиши ESC */

/* --- стороны: у каждой стороны свой "туман войны" --- */
enum { GAME_SIDE_PLAYER = 0, GAME_SIDE_CPU, GAME_SIDE_COUNT };

/* --------------------------------------------------------------- состояние */

static plat_image *g_tiles = NULL;
static plat_image *g_hudimg = NULL; /* атлас img/hudimages.png             */
static game_map    g_map;
static camera      g_cam;
static char        g_map_path[256];

/* "туман войны": отдельный массив раскрытых ячеек на каждую сторону;
 * 1 — ячейка раскрыта (раскрытое не сбрасывается никогда) */
static unsigned char *g_fog[GAME_SIDE_COUNT];

static int g_units[GAME_SIDE_COUNT];   /* юниты сторон при старте карты */
static int g_turn = GAME_SIDE_PLAYER;  /* первый ход — за игроком       */

static int g_selected  = -1;           /* выбранный объект (индекс)     */
static int g_info_open = 0;            /* открыто окно описания         */
static int g_quit      = 0;            /* выход из игрового режима      */

static unsigned g_last_ticks = 0;

/* --- строки интерфейса (Windows-1251) --- */
static char g_txt_turn_player[32];  /* "игрок"                             */
static char g_txt_turn_cpu[32];     /* "компьютер"                         */
static char g_fmt_turn[64];         /* "Ход: %s"                           */
static char g_fmt_units[96];        /* "Юниты: игрок %d | компьютер %d"    */
static char g_fmt_selected[128];    /* "Выбран: %s"                        */
static char g_txt_hint[160];        /* подсказка управления                */
static char g_fmt_ctrl[64];         /* "Управление: %s"                    */
static char g_fmt_vis[80];          /* "Дальность видимости: %d"           */
static char g_fmt_damage[64];       /* "Повреждение: %d"                   */
static char g_fmt_reload[96];       /* "Перезарядка (в ходах): %d"         */
static char g_fmt_mag[64];          /* "Магазин: %d"                       */
static char g_fmt_count[64];        /* "В наборе: %d из %d"                */
static char g_fmt_weight[64];       /* "Вес: %.1f кг"                      */
static char g_fmt_health[64];       /* "Здоровье: %d/%d"                   */
static char g_txt_class[OBJ_CLASS_COUNT][32]; /* "Тип: оружие" и т.п.     */
static char g_txt_btn_close[16];    /* глиф-крестик окна описания          */

static void init_strings(void)
{
    utf8_to_win1251("игрок", g_txt_turn_player, sizeof(g_txt_turn_player));
    utf8_to_win1251("компьютер", g_txt_turn_cpu, sizeof(g_txt_turn_cpu));
    utf8_to_win1251("Ход: %s", g_fmt_turn, sizeof(g_fmt_turn));
    utf8_to_win1251("Юниты: игрок %d | компьютер %d",
                    g_fmt_units, sizeof(g_fmt_units));
    utf8_to_win1251("Выбран: %s", g_fmt_selected, sizeof(g_fmt_selected));
    utf8_to_win1251("WASD камера | ЛКМ выбор объекта | I описание | ESC меню",
                    g_txt_hint, sizeof(g_txt_hint));
    utf8_to_win1251("Управление: %s", g_fmt_ctrl, sizeof(g_fmt_ctrl));
    utf8_to_win1251("Дальность видимости: %d", g_fmt_vis, sizeof(g_fmt_vis));
    utf8_to_win1251("Повреждение: %d", g_fmt_damage, sizeof(g_fmt_damage));
    utf8_to_win1251("Перезарядка (в ходах): %d",
                    g_fmt_reload, sizeof(g_fmt_reload));
    utf8_to_win1251("Магазин: %d", g_fmt_mag, sizeof(g_fmt_mag));
    utf8_to_win1251("В наборе: %d из %d", g_fmt_count, sizeof(g_fmt_count));
    utf8_to_win1251("Вес: %.1f кг", g_fmt_weight, sizeof(g_fmt_weight));
    utf8_to_win1251("Здоровье: %d/%d", g_fmt_health, sizeof(g_fmt_health));
    utf8_to_win1251("Тип: оружие", g_txt_class[OBJ_CLASS_WEAPON],
                    sizeof(g_txt_class[0]));
    utf8_to_win1251("Тип: боеприпас", g_txt_class[OBJ_CLASS_AMMO],
                    sizeof(g_txt_class[0]));
    utf8_to_win1251("Тип: аптечка", g_txt_class[OBJ_CLASS_MEDKIT],
                    sizeof(g_txt_class[0]));
    utf8_to_win1251("Тип: поверап", g_txt_class[OBJ_CLASS_POWERUP],
                    sizeof(g_txt_class[0]));
    utf8_to_win1251("X", g_txt_btn_close, sizeof(g_txt_btn_close));
}

/* ------------------------------------------------------------ юниты/стороны */

static int is_unit_type(int type)
{
    return type >= OBJ_TYPE_CHAR_FIRST &&
           type < OBJ_TYPE_CHAR_FIRST + OBJ_TYPE_CHAR_COUNT;
}

/* Сторона юнита: по флагу player типа персонажа (config/editor.cfg). */
static int unit_side(int type)
{
    return map_char_types[type].player ? GAME_SIDE_PLAYER : GAME_SIDE_CPU;
}

/* Дальность видимости юнита. Юниты на карте пока не связаны с конкретными
 * персонажами {characterN} из config/objects.cfg, поэтому у всех юнитов
 * дефолтная дальность (12); при связывании значение будет браться из
 * objects_character(...)->vis_range. */
static int unit_vis_range(void)
{
    return CHAR_DEFAULT_VIS_RANGE;
}

/* ------------------------------------------------------- "туман войны" */

/* Рейтингрейсинг "тумана войны": из центра ячейки юнита во все стороны
 * выходят лучи; каждый шаг луча раскрывает ячейку, в которой луч находится.
 * Если по трассировке луча встречается блок запрета передвижения
 * (m->blocks), то сама ячейка блока раскрывается (ее видно), но луч
 * дальше не идет — раскрытие тумана за блоком не распространяется.
 * Флаги раскрытых ячеек никогда не сбрасываются. */
#define FOG_RAY_COUNT 720   /* лучей по кругу из юнита               */
#define FOG_RAY_STEP  0.35f /* шаг трассировки, элементов карты      */

static void fog_reveal(unsigned char *fog, const game_map *m,
                       float ux, float uy, int radius)
{
    if (!fog || !m || m->w < 1 || m->h < 1 || radius <= 0)
        return;
    float fx = ux + 0.5f;   /* центр ячейки юнита */
    float fy = uy + 0.5f;
    float max_dist = (float)radius;
    for (int a = 0; a < FOG_RAY_COUNT; a++) {
        float ang = (float)a * (2.0f * 3.14159265f / (float)FOG_RAY_COUNT);
        float dx = cosf(ang) * FOG_RAY_STEP;
        float dy = sinf(ang) * FOG_RAY_STEP;
        float x = fx, y = fy;
        float dist = 0.0f;
        while (dist <= max_dist) {
            int cx = (int)floorf(x), cy = (int)floorf(y);
            if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h)
                break;                    /* луч вышел за пределы карты */
            int cell = cy * m->w + cx;
            fog[cell] = 1;
            if (m->blocks[cell])
                break;                    /* блок запрета — луч остановлен */
            x += dx;
            y += dy;
            dist += FOG_RAY_STEP;
        }
    }
}

/* Начало хода стороны: раскрытие тумана войны вокруг всех юнитов стороны.
 * Вызывается при старте карты для обеих сторон; в дальнейшем — для
 * юнитов стороны при старте каждого нового хода игрока или компьютера. */
static void game_turn_begin(int side)
{
    for (int i = 0; i < g_map.object_count; i++) {
        const map_object *o = &g_map.objects[i];
        if (!is_unit_type(o->type) || unit_side(o->type) != side)
            continue;
        fog_reveal(g_fog[side], &g_map, o->x, o->y, unit_vis_range());
    }
}

/* Подсчет юнитов сторон: победа в дальнейшем присваивается стороне,
 * обнулившей количество юнитов оппонента (поэтому считаем раздельно). */
static void game_count_units(void)
{
    g_units[GAME_SIDE_PLAYER] = 0;
    g_units[GAME_SIDE_CPU] = 0;
    for (int i = 0; i < g_map.object_count; i++) {
        const map_object *o = &g_map.objects[i];
        if (is_unit_type(o->type))
            g_units[unit_side(o->type)]++;
    }
}

/* ------------------------------------------------- окно описания объекта */

static void info_close(void)
{
    g_info_open = 0;
    ui_buttons_clear();
}

static void cb_info_close(void)
{
    info_close(); /* крестик-кнопка в правом верхнем углу окна */
}

static void info_open(void)
{
    g_info_open = 1;
    ui_buttons_clear();

    /* крестик-кнопка закрытия в правом верхнем углу окна описания */
    int sw = plat_screen_width();
    int csz = 44;                     /* размер кнопки, пикселей */
    int wx = sw / 2;
    int ww = sw - wx;
    int idx = ui_button_add(wx + ww - csz - 12, 12, csz, csz, "",
                            cb_info_close);
    if (idx >= 0)
        ui_button_set_glyph(idx, g_txt_btn_close);
}

/* ------------------------------------------------------------- выбор мышью */

/* Выбор объекта в ячейке под курсором; выбирается только то, что раскрыто
 * из тумана войны игрока. Клик по пустой или не раскрытой ячейке снимает
 * выбор. */
static void game_select_at(int mx, int my)
{
    float wx, wy;
    map_screen_to_world(&g_cam, mx, my, &wx, &wy);
    int cx = (int)floorf(wx), cy = (int)floorf(wy);
    g_selected = -1;
    if (cx < 0 || cy < 0 || cx >= g_map.w || cy >= g_map.h)
        return;
    if (!g_fog[GAME_SIDE_PLAYER][cy * g_map.w + cx])
        return;                  /* в тумане войны ничего не выбирается */
    for (int i = 0; i < g_map.object_count; i++) {
        const map_object *o = &g_map.objects[i];
        if ((int)floorf(o->x) == cx && (int)floorf(o->y) == cy) {
            g_selected = i;
            return;
        }
    }
}

/* ------------------------------------------------------------- ввод */

static void game_handle_keys(void)
{
    int key = plat_last_key();
    if (key == 0)
        return;
    if (key == 'i' || key == 'I') {
        /* окно описания: открывается по "I", если выбран объект,
         * закрывается повторным нажатием */
        if (g_selected >= 0) {
            if (g_info_open)
                info_close();
            else
                info_open();
        }
        return;
    }
    if (key == GAME_KEY_ESC) {
        if (g_info_open)
            info_close();     /* первый ESC закрывает окно описания */
        else
            g_quit = 1;       /* второй — выход в главное меню */
    }
}

static void game_handle_mouse(void)
{
    int press   = plat_mouse_left_pressed();
    int release = plat_mouse_left_released();
    int down    = plat_mouse_left_down();

    int window_open = g_info_open;   /* окно открыто на начало кадра */

    /* кнопки окна описания (крестик) обрабатываются первыми */
    ui_buttons_process_ext(press, release, down);
    if (!press)
        return;

    /* пока окно описания открыто, клики уходят в окно (модальное):
     * крестик уже обработан выше, остальные клики объекты не выбирают
     * и выделение не снимают */
    if (window_open)
        return;

    int mx, my;
    plat_mouse_pos(&mx, &my);
    game_select_at(mx, my);
    if (g_selected < 0)
        info_close();        /* выбор снят — окно описания закрывается */
}

/* ------------------------------------------------------------- отрисовка */

static void draw_top_bar(void)
{
    int sh = plat_screen_height();
    char buf[256];

    float size1 = 0.018f;
    float size2 = 0.016f;
    int line_h = (int)(size1 * (float)sh) + 6;

    snprintf(buf, sizeof(buf), g_fmt_turn,
             g_turn == GAME_SIDE_PLAYER ? g_txt_turn_player : g_txt_turn_cpu);
    draw_string(buf, 8, 6, size1);

    snprintf(buf, sizeof(buf), g_fmt_units,
             g_units[GAME_SIDE_PLAYER], g_units[GAME_SIDE_CPU]);
    draw_string(buf, 8, 6 + line_h, size1);

    const char *name = NULL;
    if (g_selected >= 0 && g_selected < g_map.object_count) {
        const map_object *o = &g_map.objects[g_selected];
        if (is_unit_type(o->type))
            name = objects_faction_name((char_faction)o->type);
        else {
            const game_object_desc *d =
                objects_desc(o->type - OBJ_TYPE_WEAPON_FIRST);
            if (d)
                name = d->name;
        }
    }
    if (name) {
        snprintf(buf, sizeof(buf), g_fmt_selected, name);
        draw_string(buf, 8, 6 + 2 * line_h, size2);
        draw_string(g_txt_hint, 8, 6 + 3 * line_h, size2);
    } else {
        draw_string(g_txt_hint, 8, 6 + 2 * line_h, size2);
    }
}

/* Тайл персонажа 128x128 в окне описания — по состоянию здоровья:
 * полное здоровье, частичное или нулевое (мертв). */
static char_tile_state char_tile_state_of(const map_object *o)
{
    if (o->health <= 0)
        return CHAR_TILE_DEAD;
    if (o->health < o->max_health)
        return CHAR_TILE_DAMAGED;
    return CHAR_TILE_FULL;
}

/* Окно описания выбранного объекта — в правой половине окна игры. */
static void draw_info_window(void)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    int wx = sw / 2, wy = 0, ww = sw - wx, wh = sh;
    ui_window_draw(wx, wy, ww, wh);
    if (g_selected < 0 || g_selected >= g_map.object_count)
        return;                    /* выбор уже снят — окно пустое */
    const map_object *o = &g_map.objects[g_selected];

    char title[OBJECT_NAME_MAX];
    char line[8][128];
    int n = 0;

    if (is_unit_type(o->type)) {
        /* юнит: на карте пока только тип персонажа (фракция); конкретные
         * описания {characterN} будут привязываться на следующих этапах */
        char_faction faction = (char_faction)o->type;
        const char *fname = objects_faction_name(faction);
        snprintf(title, sizeof(title), "%s", fname ? fname : "?");
        snprintf(line[n++], sizeof(line[0]), g_fmt_ctrl,
                 map_char_types[o->type].player
                     ? g_txt_turn_player : g_txt_turn_cpu);
        snprintf(line[n++], sizeof(line[0]), g_fmt_vis, unit_vis_range());
        if (n < 8)
            snprintf(line[n++], sizeof(line[0]), g_fmt_health,
                     o->health, o->max_health);

        /* тайл персонажа 128x128 (img/hudimages.png) в правом верхнем
         * углу окна — по текущему количеству здоровья */
        int tx_img, ty_img;
        if (g_hudimg &&
            objects_faction_hud_img(faction, char_tile_state_of(o),
                                    &tx_img, &ty_img) == 0) {
            int isz = ww / 3;
            if (isz > wh / 3)
                isz = wh / 3;
            if (isz > OBJECT_HUD_IMG_PX * 2)
                isz = OBJECT_HUD_IMG_PX * 2;
            plat_draw_image(g_hudimg, tx_img, ty_img,
                            OBJECT_HUD_IMG_PX, OBJECT_HUD_IMG_PX,
                            wx + ww - isz - 24, wy + 44, isz, isz);
        }
    } else {
        const game_object_desc *d =
            objects_desc(o->type - OBJ_TYPE_WEAPON_FIRST);
        if (!d) {
            snprintf(title, sizeof(title), "%s", "?");
        } else {
            snprintf(title, sizeof(title), "%s", d->name);
            if (n < 8)
                snprintf(line[n++], sizeof(line[0]), "%s",
                         g_txt_class[d->cls]);
            for (int k = 0; k < OBJECT_DESC_LINES && n < 8; k++)
                if (d->desc[k][0])
                    snprintf(line[n++], sizeof(line[0]), "%s", d->desc[k]);
            if (d->cls == OBJ_CLASS_WEAPON) {
                if (n < 8)
                    snprintf(line[n++], sizeof(line[0]), g_fmt_damage,
                             d->damage);
                if (n < 8)
                    snprintf(line[n++], sizeof(line[0]), g_fmt_reload,
                             d->reload_turns);
                if (n < 8)
                    snprintf(line[n++], sizeof(line[0]), g_fmt_mag,
                             d->magazine_size);
            } else if (n < 8) {
                snprintf(line[n++], sizeof(line[0]), g_fmt_count,
                         d->count, d->max_count);
            }
            if (n < 8)
                snprintf(line[n++], sizeof(line[0]), g_fmt_weight,
                         d->weight_kg);
        }
    }

    float size_t = 0.02f;
    float size_l = 0.016f;
    int lh = (int)(size_l * (float)sh) + 8;
    int x = wx + 24;
    int y = wy + 20;
    draw_string(title, x, y, size_t);
    y += lh + 6;
    for (int i = 0; i < n; i++) {
        draw_string(line[i], x, y, size_l);
        y += lh;
    }

    /* крестик-кнопка закрытия добавлена в info_open() */
    ui_buttons_draw();
}

static void game_draw(void)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();

    plat_clear_screen(0, 0, 0);

    /* карта и объекты; в тумане войны игрока ничего не рисуется */
    map_draw(&g_map, &g_cam, g_tiles, g_fog[GAME_SIDE_PLAYER], 0);
    draw_top_bar();

    /* мигающая рамка выделенного объекта: светло-синяя у юнитов игрока,
     * зеленая у прочих объектов (юниты противника, предметы) */
    if (g_selected >= 0 && g_selected < g_map.object_count) {
        const map_object *o = &g_map.objects[g_selected];
        float tpx = map_tile_px(&g_cam);
        int tpi = tpx >= 1.0f ? (int)tpx : 1;
        int sx = (int)((o->x - g_cam.x) * tpx + (float)sw / 2.0f);
        int sy = (int)((o->y - g_cam.y) * tpx + (float)sh / 2.0f);
        unsigned char cr, cg, cb;
        if (is_unit_type(o->type) && unit_side(o->type) == GAME_SIDE_PLAYER) {
            cr = 120; cg = 180; cb = 255;   /* светло-синий */
        } else {
            cr = 90; cg = 230; cb = 90;     /* зеленый */
        }
        plat_draw_blink_frame(sx - 2, sy - 2, tpi + 4, tpi + 4, cr, cg, cb);
    }

    if (g_info_open)
        draw_info_window();

    plat_flip_frame();
}

/* ------------------------------------------------------- жизненный цикл */

/* Путь к карте: имя без пути открывается в папке maps/ (как в редакторе). */
static void build_map_path(const char *name)
{
    if (!name || !*name)
        name = "map1.map";
    if (strncmp(name, MAP_DIR "/", 5) == 0 || strchr(name, '/') != NULL)
        snprintf(g_map_path, sizeof(g_map_path), "%s", name);
    else
        snprintf(g_map_path, sizeof(g_map_path), MAP_DIR "/%s", name);
}

int game_init(const char *map_filename)
{
    ui_buttons_clear();

    if (map_registry_init(GAME_CFG) != 0)
        return -1;

    /* тайлы загружаются с color key (черный фон тайловых листов прозрачен);
     * атлас HUD нужен для тайлов персонажей в окне описания */
    g_tiles = plat_load_image_colorkey(GTILES_PATH, 0, 0, 0);
    if (!g_tiles) {
        fprintf(stderr, "game: не удалось загрузить '%s'\n", GTILES_PATH);
        return -1;
    }
    g_hudimg = plat_load_image_colorkey(HUDIMG_PATH, 0, 0, 0);
    if (!g_hudimg) {
        fprintf(stderr, "game: не удалось загрузить '%s'\n", HUDIMG_PATH);
        plat_free_image(g_tiles);
        g_tiles = NULL;
        return -1;
    }

    build_map_path(map_filename);
    if (map_load(&g_map, g_map_path) != 0) {
        fprintf(stderr, "game: карта '%s' не загружена\n", g_map_path);
        plat_free_image(g_tiles);
        g_tiles = NULL;
        plat_free_image(g_hudimg);
        g_hudimg = NULL;
        return -1;
    }

    /* камера и первое позиционирование — на первом юните игрока */
    camera_init(&g_cam, map_vis_elements(), map_camera_speed(),
                (float)g_map.w, (float)g_map.h);
    for (int i = 0; i < g_map.object_count; i++) {
        const map_object *o = &g_map.objects[i];
        if (is_unit_type(o->type) && unit_side(o->type) == GAME_SIDE_PLAYER) {
            g_cam.x = o->x + 0.5f;
            g_cam.y = o->y + 0.5f;
            break;
        }
    }
    camera_clamp(&g_cam);

    /* "туман войны": по массиву раскрытых ячеек на каждую сторону */
    for (int s = 0; s < GAME_SIDE_COUNT; s++) {
        g_fog[s] = (unsigned char *)
            calloc((size_t)g_map.w * (size_t)g_map.h, 1);
        if (!g_fog[s]) {
            for (int k = 0; k < s; k++) {
                free(g_fog[k]);
                g_fog[k] = NULL;
            }
            map_free(&g_map);
            plat_free_image(g_tiles);
            g_tiles = NULL;
            plat_free_image(g_hudimg);
            g_hudimg = NULL;
            return -1;
        }
    }

    game_count_units();
    g_turn = GAME_SIDE_PLAYER;            /* первый ход — игрок */

    /* сразу раскрыть туман войны вокруг юнитов обеих сторон */
    game_turn_begin(GAME_SIDE_PLAYER);
    game_turn_begin(GAME_SIDE_CPU);

    g_selected = -1;
    g_info_open = 0;
    g_quit = 0;
    init_strings();

    fprintf(stderr, "game: карта '%s' (%dx%d); юниты: игрок %d, "
                    "компьютер %d\n", g_map_path, g_map.w, g_map.h,
            g_units[GAME_SIDE_PLAYER], g_units[GAME_SIDE_CPU]);
    return 0;
}

void game_frame(float dt)
{
    game_handle_keys();
    game_handle_mouse();
    camera_update(&g_cam, dt);   /* WASD — перемещение камеры */
    game_draw();
}

void game_run(void)
{
    g_last_ticks = plat_ticks();
    while (!plat_poll_events() && !g_quit) {
        unsigned now = plat_ticks();
        float dt = (float)(now - g_last_ticks) / 1000.0f;
        g_last_ticks = now;
        game_frame(dt);
    }
}

void game_shutdown(void)
{
    for (int s = 0; s < GAME_SIDE_COUNT; s++) {
        free(g_fog[s]);
        g_fog[s] = NULL;
    }
    map_free(&g_map);
    plat_free_image(g_tiles);
    g_tiles = NULL;
    plat_free_image(g_hudimg);
    g_hudimg = NULL;
    ui_buttons_clear();
}
