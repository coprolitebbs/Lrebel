/*
 * editor.c — реализация редактора карт.
 *
 * Отрисовка — через portable_posix.h (изображения, примитивы), шрифт
 * (font.h), типовые окна и кнопки (ui.h) и функции карты (map.h).
 * Надписи хранятся в Windows-1251 и собираются один раз при editor_init.
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
#include "inventory.h"
#include "inventory_ui.h"
#include "editor.h"

#define EDITOR_CFG       "config/editor.cfg"
#define GTILES_PATH      "img/gtiles.png"
#define HUDIMG_PATH      "img/hudimages.png" /* тайлы HUD и картинки инвентаря */
#define MAP_DIR          "maps"
#define DEFAULT_MAP_NAME "unnamed.map"

#define EDITOR_KEY_ESC   27  /* ASCII-код клавиши ESC                    */
#define EDITOR_KEY_SPACE 32
#define EDITOR_KEY_DEL   127 /* ASCII-код клавиши DEL (SDLK_DELETE)      */

/* --- окно параметров персонажа (P) --- */
#define PARAMS_ROW_H     46  /* высота строки параметра, пикселей        */
#define PARAMS_HEADER_H  130 /* заголовок + строки описания до параметров */

/* --- режимы редактора (клавиши 1-4) --- */
enum {
    ED_MODE_TILES = 0,  /* поверхности и стены, слои 1-3 */
    ED_MODE_OBJECTS,    /* объекты: персонажи и оружие   */
    ED_MODE_ASSETS,     /* ассеты, верхний слой 4        */
    ED_MODE_BLOCKS,     /* невидимые блоки запрета       */
    ED_MODE_COUNT
};

/* --- модальные окна --- */
enum {
    ED_MODAL_NONE = 0,
    ED_MODAL_PICKER,    /* выбор тайла/объекта/ассета (SPACE)            */
    ED_MODAL_INVPICK,   /* выбор предмета для инвентаря (O)              */
    ED_MODAL_PARAMS,    /* параметры персонажа (P)                       */
    ED_MODAL_CONFIRM
};

/* --- окно выбора (SPACE): сетка тайлов --- */
#define PICK_CELL   40  /* ячейка сетки выбора, пикселей */
#define PICK_COLS   16  /* колонок в окне выбора         */
#define PICK_PAD    10
#define PICK_HEADER 40  /* высота заголовка окна выбора  */

static plat_image *g_tiles = NULL;
static plat_image *g_hudimg = NULL; /* атлас img/hudimages.png для инвентаря */
static game_map    g_map;
static camera      g_cam;
static char        g_map_path[256];

static int g_mode = ED_MODE_TILES;
static int g_layer = 1;        /* слой поверхностей 1..3 (режим 1) */
static int g_cur_tile = 0;     /* выбранный тайл поверхности/стены */
static int g_cur_object = 0;   /* выбранный тип объекта            */
static int g_cur_asset = 0;    /* выбранный ассет (номер)          */

/* "текущий выбранный объект" (индекс в g_map.objects) или -1: выбирается
 * ПКМ в режиме объектов, при постановке ЛКМ, удаляется по DEL; у каждого
 * объекта карты свой инвентарь (создается лениво, см. map_object_inventory,
 * и сохраняется в файл карты вместе с объектом) */
static int g_selected = -1;

/* выбранный параметр в окне параметров (P) или -1; выбирается кликом,
 * изменяется клавишами +/- в пределах 0..100 (только в редакторе) */
static int g_param_sel = -1;

static int      g_modal = ED_MODAL_NONE;
static int      g_picker_scroll = 0; /* верхняя строка окна выбора */
static int      g_quit = 0;
static unsigned g_last_ticks = 0;

/* --- строки интерфейса (Windows-1251) --- */
static char g_txt_modes[ED_MODE_COUNT][64];
static char g_txt_hint[160];       /* общая подсказка (режимы 3-4)      */
static char g_txt_hint_tiles[160]; /* подсказка режима поверхностей    */
static char g_txt_hint_objects[2][160]; /* подсказка режима объектов (2 строки) */
static char g_fmt_bar_tiles[96];   /* "Режим: %s | Слой: %d/3 | Тайл: %d" */
static char g_fmt_bar_objects[96]; /* "Режим: %s | Тип: %d/%d | Выбран: %s | %s" */
static char g_fmt_bar_assets[64];  /* "Режим: %s | Ассет: %d/%d"          */
static char g_txt_bar_blocks[96];
static char g_txt_saved[32], g_txt_modified[32];
static char g_txt_pick_title[ED_MODE_COUNT][96];
static char g_txt_pick_title_inv[96]; /* заголовок окна добавления предмета */
static char g_txt_pick_hint[128];
static char g_txt_pick_hint_inv[128];
static char g_txt_sel_none[16];    /* "нет"                             */
static char g_fmt_sel_obj[32];     /* "объект %d"                       */
static char g_txt_confirm_q[96];
static char g_txt_btn_save[32], g_txt_btn_cancel[32];

/* --- окно параметров (P): надписи --- */
static char g_fmt_ctrl[64];        /* "Управление: %s"                  */
static char g_fmt_vis[80];         /* "Дальность видимости: %d"         */
static char g_fmt_steps[64];       /* "Шагов за ход: %d"                */
static char g_txt_params[CHAR_PARAM_COUNT][64]; /* названия параметров  */
static char g_txt_yes[16], g_txt_no[16];        /* "да"/"нет"           */
static char g_txt_params_hint[160];             /* подсказка окна       */
static char g_txt_player[16], g_txt_cpu[16];    /* "игрок"/"компьютер"  */

/* прототип: используется окном параметров (определен в разделе правок) */
static int is_char_type(int type);

static void init_strings(void)
{
    utf8_to_win1251("Поверхности и стены", g_txt_modes[0],
                    sizeof(g_txt_modes[0]));
    utf8_to_win1251("Объекты", g_txt_modes[1], sizeof(g_txt_modes[1]));
    utf8_to_win1251("Ассеты", g_txt_modes[2], sizeof(g_txt_modes[2]));
    utf8_to_win1251("Невидимые блоки", g_txt_modes[3],
                    sizeof(g_txt_modes[3]));
    utf8_to_win1251("WASD камера | SPACE выбор | 1-4 режимы | +/- слой "
                    "| Ctrl+S сохранить | ESC выход",
                    g_txt_hint, sizeof(g_txt_hint));
    utf8_to_win1251("WASD камера | SPACE выбор | ЛКМ рисование, ПКМ взять "
                    "тайл | +/- слой | Ctrl+S сохранить | ESC выход",
                    g_txt_hint_tiles, sizeof(g_txt_hint_tiles));
    utf8_to_win1251("WASD камера | SPACE выбор | ЛКМ поставить, ПКМ выбрать "
                    "| DEL удалить",
                    g_txt_hint_objects[0], sizeof(g_txt_hint_objects[0]));
    utf8_to_win1251("I инвентарь | P параметры | O предмет | Ctrl+S сохранить "
                    "| ESC выход",
                    g_txt_hint_objects[1], sizeof(g_txt_hint_objects[1]));
    utf8_to_win1251("Режим: %s | Слой: %d/3 | Тайл: %d | %s",
                    g_fmt_bar_tiles, sizeof(g_fmt_bar_tiles));
    utf8_to_win1251("Режим: %s | Тип: %d/%d | Выбран: %s | %s",
                    g_fmt_bar_objects, sizeof(g_fmt_bar_objects));
    utf8_to_win1251("Режим: %s | Ассет: %d/%d | %s",
                    g_fmt_bar_assets, sizeof(g_fmt_bar_assets));
    utf8_to_win1251("Режим: Невидимые блоки | ЛКМ запрет, ПКМ очистка",
                    g_txt_bar_blocks, sizeof(g_txt_bar_blocks));
    utf8_to_win1251("сохранена", g_txt_saved, sizeof(g_txt_saved));
    utf8_to_win1251("ИЗМЕНЕНА", g_txt_modified, sizeof(g_txt_modified));
    utf8_to_win1251("Выберите тайл поверхности или стены",
                    g_txt_pick_title[0], sizeof(g_txt_pick_title[0]));
    utf8_to_win1251("Выберите объект",
                    g_txt_pick_title[1], sizeof(g_txt_pick_title[1]));
    utf8_to_win1251("Выберите ассет",
                    g_txt_pick_title[2], sizeof(g_txt_pick_title[2]));
    utf8_to_win1251("Выбор: клик | Закрыть: SPACE или ESC | Прокрутка: +/-",
                    g_txt_pick_hint, sizeof(g_txt_pick_hint));
    utf8_to_win1251("Выберите предмет для инвентаря",
                    g_txt_pick_title_inv, sizeof(g_txt_pick_title_inv));
    utf8_to_win1251("Клик - добавить в инвентарь | Закрыть: SPACE или ESC",
                    g_txt_pick_hint_inv, sizeof(g_txt_pick_hint_inv));
    utf8_to_win1251("нет", g_txt_sel_none, sizeof(g_txt_sel_none));
    utf8_to_win1251("объект %d", g_fmt_sel_obj, sizeof(g_fmt_sel_obj));
    utf8_to_win1251("Карта изменена. Сохранить перед выходом?",
                    g_txt_confirm_q, sizeof(g_txt_confirm_q));
    utf8_to_win1251("Сохранить", g_txt_btn_save, sizeof(g_txt_btn_save));
    utf8_to_win1251("Отменить", g_txt_btn_cancel, sizeof(g_txt_btn_cancel));

    /* --- окно параметров (P) --- */
    utf8_to_win1251("Управление: %s", g_fmt_ctrl, sizeof(g_fmt_ctrl));
    utf8_to_win1251("Дальность видимости: %d", g_fmt_vis, sizeof(g_fmt_vis));
    utf8_to_win1251("Шагов за ход: %d", g_fmt_steps, sizeof(g_fmt_steps));
    utf8_to_win1251("Скорость", g_txt_params[CHAR_PARAM_SPEED],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Выносливость", g_txt_params[CHAR_PARAM_ENDURANCE],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Меткость", g_txt_params[CHAR_PARAM_ACCURACY],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Мораль", g_txt_params[CHAR_PARAM_MORALE],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Здоровье", g_txt_params[CHAR_PARAM_HEALTH],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Броня", g_txt_params[CHAR_PARAM_ARMOR],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("Командир", g_txt_params[CHAR_PARAM_COMMANDER],
                    sizeof(g_txt_params[0]));
    utf8_to_win1251("да", g_txt_yes, sizeof(g_txt_yes));
    utf8_to_win1251("нет", g_txt_no, sizeof(g_txt_no));
    utf8_to_win1251("игрок", g_txt_player, sizeof(g_txt_player));
    utf8_to_win1251("компьютер", g_txt_cpu, sizeof(g_txt_cpu));
    utf8_to_win1251("Клик - выбор параметра | +/- изменить | P или ESC закрыть",
                    g_txt_params_hint, sizeof(g_txt_params_hint));
}

/* ------------------------------------------------------------- выборка */

static int picker_item_count(void)
{
    if (g_modal == ED_MODAL_INVPICK)
        return map_pickable_count(); /* предметы для инвентаря (строка 4) */
    switch (g_mode) {
    case ED_MODE_TILES:
        return map_walls_last_tile() - map_surface_first_tile() + 1;
    case ED_MODE_OBJECTS:
        return map_object_type_count();
    case ED_MODE_ASSETS:
        return map_asset_count();
    default:
        return 0;
    }
}

static int picker_item_tile(int idx)
{
    if (g_modal == ED_MODAL_INVPICK)
        return map_object_tile(OBJ_TYPE_WEAPON_FIRST + idx, OBJ_DIR_DOWN);
    switch (g_mode) {
    case ED_MODE_TILES:
        return map_surface_first_tile() + idx;
    case ED_MODE_OBJECTS:
        return map_object_tile(idx, OBJ_DIR_DOWN);
    case ED_MODE_ASSETS:
        return map_asset_tile(idx);
    default:
        return -1;
    }
}

static int current_selection_index(void)
{
    switch (g_mode) {
    case ED_MODE_TILES:
        return g_cur_tile - map_surface_first_tile();
    case ED_MODE_OBJECTS:
        return g_cur_object;
    case ED_MODE_ASSETS:
        return g_cur_asset;
    default:
        return 0;
    }
}

static void picker_apply(int idx)
{
    switch (g_mode) {
    case ED_MODE_TILES:
        g_cur_tile = map_surface_first_tile() + idx;
        break;
    case ED_MODE_OBJECTS:
        g_cur_object = idx;
        break;
    case ED_MODE_ASSETS:
        g_cur_asset = idx;
        break;
    default:
        break;
    }
}

/* Тайл текущего глифа для верхней строки. */
static int current_glyph_tile(void)
{
    switch (g_mode) {
    case ED_MODE_TILES:
        return g_cur_tile;
    case ED_MODE_OBJECTS:
        return map_object_tile(g_cur_object, OBJ_DIR_DOWN);
    case ED_MODE_ASSETS:
        return map_asset_tile(g_cur_asset);
    default:
        return -1;
    }
}

static int picker_rows_visible(void)
{
    int rows = (plat_screen_height() - 140) / PICK_CELL;
    if (rows < 3)
        rows = 3;
    if (rows > 12)
        rows = 12;
    return rows;
}

/* Число строк, реально занимаемое содержимым окна выбора: окно
 * добавления предметов (O) делается компактным — высотой по числу
 * предметов (в отличие от полноэкранных окон выбора тайлов). */
static int picker_rows_used(void)
{
    int rows = picker_rows_visible();
    int need = (picker_item_count() + PICK_COLS - 1) / PICK_COLS;
    if (need < rows)
        rows = need;
    if (rows < 1)
        rows = 1;
    return rows;
}

/* Прямоугольник окна выбора (общий для отрисовки и кликов). */
static void picker_rect(int *px, int *py, int *pw, int *ph, int rows)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    *pw = PICK_COLS * PICK_CELL + 2 * PICK_PAD + 24;
    *ph = PICK_HEADER + rows * PICK_CELL + 2 * PICK_PAD + 24;
    *px = (sw - *pw) / 2;
    *py = (sh - *ph) / 2;
}

static void open_picker(void)
{
    int count = picker_item_count();
    if (count <= 0)
        return;
    g_modal = ED_MODAL_PICKER;

    /* прокрутка так, чтобы текущий выбор был виден */
    int rows = picker_rows_visible();
    int total_rows = (count + PICK_COLS - 1) / PICK_COLS;
    int sel_row = current_selection_index() / PICK_COLS;
    g_picker_scroll = sel_row - rows / 2;
    if (g_picker_scroll < 0)
        g_picker_scroll = 0;
    if (g_picker_scroll > total_rows - rows)
        g_picker_scroll = total_rows - rows;
    if (g_picker_scroll < 0)
        g_picker_scroll = 0;
}

/* Окно добавления предмета в инвентарь (клавиша O): открывается, только
 * если есть "текущий выбранный объект" — предмет добавляется в его
 * инвентарь. */
static void open_invpick(void)
{
    if (g_selected < 0 || map_pickable_count() <= 0)
        return;
    g_modal = ED_MODAL_INVPICK;
    g_picker_scroll = 0;
}

/* ---------------------------------------------------- окна и кнопки */

static void editor_close_modals(void)
{
    g_modal = ED_MODAL_NONE;
    ui_buttons_clear();
}

static void cb_confirm_cancel(void)
{
    editor_close_modals();
}

static void cb_confirm_save(void)
{
    if (map_save(&g_map, g_map_path) == 0) {
        g_map.modified = 0;
        g_quit = 1; /* сохранено — выходим */
    }
    /* при ошибке записи окно остаётся открытым */
}

static void open_confirm(void)
{
    g_modal = ED_MODAL_CONFIRM;
    ui_buttons_clear();

    int sw = plat_screen_width();
    int sh = plat_screen_height();
    int ww = 520, wh = 200;
    int wx = (sw - ww) / 2;
    int wy = (sh - wh) / 2;
    int bw = 200, bh = 56;
    ui_button_add(wx + ww / 2 - bw - 16, wy + wh - bh - 28,
                  bw, bh, g_txt_btn_save, cb_confirm_save);
    ui_button_add(wx + ww / 2 + 16, wy + wh - bh - 28,
                  bw, bh, g_txt_btn_cancel, cb_confirm_cancel);
}

/* -------------------------------------------------- окно параметров (P) */

/* Прямоугольник окна параметров (общий для отрисовки и кликов). */
static void params_rect(int *px, int *py, int *pw, int *ph)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    *pw = 620;
    *ph = PARAMS_HEADER_H + CHAR_PARAM_COUNT * PARAMS_ROW_H + 64;
    if (*pw > sw - 32)
        *pw = sw - 32;
    if (*ph > sh - 32)
        *ph = sh - 32;
    *px = (sw - *pw) / 2;
    *py = (sh - *ph) / 2;
}

/* +/- в окне параметров: изменение выбранного параметра в пределах
 * 0..100; Здоровье меняется вместе с максимумом (в редакторе повреждений
 * нет). В обычной игре параметры не редактируются (см. game.c). */
static void editor_param_adjust(int d)
{
    if (g_selected < 0 || g_selected >= g_map.object_count || g_param_sel < 0)
        return;
    if (g_param_sel >= CHAR_PARAM_COMMANDER)
        return; /* флаг "Командир" переключается кликом */
    map_object *o = &g_map.objects[g_selected];
    int *field = NULL;
    switch (g_param_sel) {
    case CHAR_PARAM_SPEED:     field = &o->stats.speed;     break;
    case CHAR_PARAM_ENDURANCE: field = &o->stats.endurance; break;
    case CHAR_PARAM_ACCURACY:  field = &o->stats.accuracy;  break;
    case CHAR_PARAM_MORALE:    field = &o->stats.morale;    break;
    case CHAR_PARAM_HEALTH:    field = &o->stats.health;    break;
    case CHAR_PARAM_ARMOR:     field = &o->stats.armor;     break;
    default:
        return;
    }
    *field += d;
    if (*field < 0)
        *field = 0;
    if (*field > 100)
        *field = 100;
    if (g_param_sel == CHAR_PARAM_HEALTH)
        o->max_health = o->stats.health;
    g_map.modified = 1;
}

/* Клик в окне параметров: выбор строки параметра (рамка); клик по строке
 * "Командир" переключает флаг. */
static void editor_params_click(void)
{
    int mx, my;
    plat_mouse_pos(&mx, &my);
    int px, py, pw, ph;
    params_rect(&px, &py, &pw, &ph);
    if (mx < px || mx >= px + pw || my < py || my >= py + ph)
        return;
    if (g_selected < 0 || g_selected >= g_map.object_count)
        return;
    const map_object *o = &g_map.objects[g_selected];
    if (!is_char_type(o->type))
        return; /* у предметов параметров нет */
    int idx = (my - (py + PARAMS_HEADER_H)) / PARAMS_ROW_H;
    if (idx < 0 || idx >= CHAR_PARAM_COUNT)
        return; /* клик по заголовку или подсказке */
    if (idx == CHAR_PARAM_COMMANDER) {
        map_object *om = &g_map.objects[g_selected];
        om->stats.commander = om->stats.commander ? 0 : 1;
        g_map.modified = 1;
        return;
    }
    g_param_sel = idx;
}

/* ------------------------------------------------------------- правки */

/* Тип — персонаж (0..2)? */
static int is_char_type(int type)
{
    return type >= OBJ_TYPE_CHAR_FIRST &&
           type < OBJ_TYPE_CHAR_FIRST + OBJ_TYPE_CHAR_COUNT;
}

static void editor_set_tile(int cx, int cy)
{
    if (g_layer < 1 || g_layer > MAP_ASSET_LAYER)
        return;
    g_map.layers[g_layer - 1][cy * g_map.w + cx] = (unsigned short)g_cur_tile;
    g_map.modified = 1;
}

/* ПКМ в режиме поверхностей и стен: берёт в качестве текущего тайл,
 * лежащий в текущем выбранном слое в ячейке под курсором (пипетка);
 * пустая ячейка выбор не меняет. */
static void editor_pick_tile(int cx, int cy)
{
    if (g_layer < 1 || g_layer > MAP_ASSET_LAYER)
        return;
    unsigned short id = g_map.layers[g_layer - 1][cy * g_map.w + cx];
    if (id == 0)
        return; /* пусто — брать нечего */
    g_cur_tile = id;
}

static void editor_set_asset(int cx, int cy)
{
    int tile = map_asset_tile(g_cur_asset);
    if (tile <= 0)
        return;
    g_map.layers[MAP_ASSET_LAYER][cy * g_map.w + cx] = (unsigned short)tile;
    g_map.modified = 1;
}

static void editor_clear_asset(int cx, int cy)
{
    g_map.layers[MAP_ASSET_LAYER][cy * g_map.w + cx] = 0;
    g_map.modified = 1;
}

static void editor_set_block(int cx, int cy)
{
    g_map.blocks[cy * g_map.w + cx] = 1;
    g_map.modified = 1;
}

static void editor_clear_block(int cx, int cy)
{
    g_map.blocks[cy * g_map.w + cx] = 0;
    g_map.modified = 1;
}

static void editor_place_object(int cx, int cy)
{
    int idx = map_object_add(&g_map, g_cur_object,
                             (float)cx, (float)cy, OBJ_DIR_DOWN);
    if (idx < 0) {
        fprintf(stderr, "editor: ячейка (%d,%d) занята объектом или "
                        "достигнут предел 256 объектов\n", cx, cy);
        return;
    }
    g_map.modified = 1;
    /* добавленный объект сразу становится "текущим выбранным объектом" */
    g_selected = idx;
}

/* ПКМ в режиме объектов: "текущий выбранный объект" — объект в ячейке
 * под курсором; пустая ячейка сбрасывает выбор. */
static void editor_select_object(int cx, int cy)
{
    g_selected = map_object_index_at(&g_map, cx, cy);
}

/* DEL: удаление "текущего выбранного объекта"; выбор сбрасывается.
 * Инвентарь удаляемого объекта освобождается, инвентари сдвигаются
 * вместе с объектами (map_object_remove_index). */
static void editor_delete_selected(void)
{
    if (g_selected < 0 || g_selected >= g_map.object_count)
        return;
    inventory_ui_close(); /* окна инвентаря/параметров закрываются вместе */
    editor_close_modals();
    map_object_remove_index(&g_map, g_selected);
    g_selected = -1; /* объект удален — "текущий выбранный" сброшен      */
    g_map.modified = 1;
}

/* ------------------------------------------------------------- клавиши */

/* +/-: слой (режим 1), тип объекта (режим 2), ассет (режим 3) */
static void step_current(int d)
{
    switch (g_mode) {
    case ED_MODE_TILES:
        g_layer += d;
        if (g_layer < 1)
            g_layer = MAP_ASSET_LAYER;
        if (g_layer > MAP_ASSET_LAYER)
            g_layer = 1;
        break;
    case ED_MODE_OBJECTS: {
        int n = map_object_type_count();
        if (n > 0)
            g_cur_object = (g_cur_object + d % n + n) % n;
        break;
    }
    case ED_MODE_ASSETS: {
        int n = map_asset_count();
        if (n > 0)
            g_cur_asset = (g_cur_asset + d % n + n) % n;
        break;
    }
    default:
        break;
    }
}

static void editor_handle_keys(void)
{
    int key = plat_last_key();
    if (key == 0)
        return;

    if (g_modal == ED_MODAL_PARAMS) {
        /* окно параметров: +/- изменяет выбранный параметр (0..100)     */
        if (key == '+' || key == '-')
            editor_param_adjust(key == '+' ? 1 : -1);
        else if (key == EDITOR_KEY_ESC || key == EDITOR_KEY_SPACE ||
                 key == 'p' || key == 'P')
            editor_close_modals();
        return;
    }
    if (g_modal == ED_MODAL_PICKER || g_modal == ED_MODAL_INVPICK) {
        if (key == '+')
            g_picker_scroll++;
        else if (key == '-')
            g_picker_scroll--;
        else if (key == EDITOR_KEY_ESC || key == EDITOR_KEY_SPACE)
            editor_close_modals();
        else
            return;
        int total_rows = (picker_item_count() + PICK_COLS - 1) / PICK_COLS;
        int rows = picker_rows_used();
        if (g_picker_scroll > total_rows - rows)
            g_picker_scroll = total_rows - rows;
        if (g_picker_scroll < 0)
            g_picker_scroll = 0;
        return;
    }
    if (g_modal == ED_MODAL_CONFIRM) {
        if (key == EDITOR_KEY_ESC)
            editor_close_modals(); /* ESC = «Отменить» */
        return;
    }

    switch (key) {
    case '1': case '2': case '3': case '4':
        g_mode = key - '1';
        break;
    case '+':
        step_current(+1);
        break;
    case '-':
        step_current(-1);
        break;
    case EDITOR_KEY_SPACE:
        open_picker();
        break;
    case EDITOR_KEY_DEL:
        /* DEL: удалить "текущий выбранный объект" (если он есть)       */
        editor_delete_selected();
        break;
    case 'i': case 'I':
        /* I: окно инвентаря выбранного объекта в левой половине окна;
         * повторное нажатие закрывает окно */
        if (g_selected >= 0) {
            if (g_modal == ED_MODAL_PARAMS)
                editor_close_modals();
            if (inventory_ui_is_open()) {
                inventory_ui_close();
            } else {
                inventory *inv = map_object_inventory(&g_map, g_selected);
                if (inv)
                    inventory_ui_open(inv, g_hudimg, 0, 0,
                                      plat_screen_width() / 2,
                                      plat_screen_height());
            }
        }
        break;
    case 'p': case 'P':
        /* P: окно параметров выбранного персонажа — описание и боевые
         * параметры (в редакторе они редактируются); у предметов —
         * описание предмета. Повторное нажатие закрывает окно */
        if (g_selected >= 0) {
            if (inventory_ui_is_open())
                inventory_ui_close();
            if (g_modal == ED_MODAL_PARAMS)
                editor_close_modals();
            else {
                g_modal = ED_MODAL_PARAMS;
                g_param_sel = -1;
                ui_buttons_clear();
            }
        }
        break;
    case 'o': case 'O':
        /* O: окно выбора предмета для добавления в инвентарь выбранного
         * объекта */
        open_invpick();
        break;
    case 's':
        if (plat_key_ctrl()) {
            if (map_save(&g_map, g_map_path) == 0) {
                g_map.modified = 0;
                fprintf(stderr, "editor: карта сохранена в '%s'\n",
                        g_map_path);
            }
        }
        break;
    case EDITOR_KEY_ESC:
        /* первый ESC закрывает окно параметров или инвентаря, далее —
         * как обычно */
        if (g_modal == ED_MODAL_PARAMS)
            editor_close_modals();
        else if (inventory_ui_is_open())
            inventory_ui_close();
        else if (!g_map.modified)
            g_quit = 1; /* сохранена и не менялась — выходим */
        else
            open_confirm();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- мышь */

static void editor_picker_click(void)
{
    int mx, my;
    plat_mouse_pos(&mx, &my);
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph, picker_rows_used());
    if (mx < px || mx >= px + pw || my < py || my >= py + ph)
        return;

    int col = (mx - px - PICK_PAD) / PICK_CELL;
    int row = (my - py - PICK_HEADER) / PICK_CELL;
    if (col < 0 || col >= PICK_COLS || row < 0 || row >= picker_rows_used())
        return;
    int idx = (g_picker_scroll + row) * PICK_COLS + col;
    if (idx < 0 || idx >= picker_item_count())
        return;

    if (g_modal == ED_MODAL_INVPICK) {
        /* предмет добавляется в инвентарь выбранного объекта, если там
         * есть место под его габариты (окно остается открытым — можно
         * добавить несколько предметов); функция сканирования места и
         * добавления — inventory_add_item (inventory.h) */
        inventory *inv = map_object_inventory(&g_map, g_selected);
        const game_object_desc *d = objects_desc(idx);
        if (!inv || !d)
            return;
        if (inventory_add_item(inv, d, NULL) != 0)
            fprintf(stderr, "editor: в инвентаре нет места для '%s'\n",
                    d->name);
        return;
    }

    picker_apply(idx);
    editor_close_modals();
}

static void editor_handle_mouse(void)
{
    int press   = plat_mouse_left_pressed();
    int release = plat_mouse_left_released();
    int down    = plat_mouse_left_down();
    int rpress  = plat_mouse_right_pressed();

    /* кнопки модальных окон обрабатываются первыми */
    ui_buttons_process_ext(press, release, down);

    if (g_modal == ED_MODAL_CONFIRM)
        return; /* клики ушли в кнопки окна */
    if (g_modal == ED_MODAL_PARAMS) {
        /* окно параметров забирает мышь: выбор строки параметра        */
        if (press)
            editor_params_click();
        return;
    }
    if (g_modal == ED_MODAL_PICKER || g_modal == ED_MODAL_INVPICK) {
        if (press)
            editor_picker_click();
        return;
    }

    /* открытое окно инвентаря (левая половина) забирает мышь целиком:
     * внутри него — выбор и перетаскивание предметов инвентаря        */
    if (inventory_ui_is_open()) {
        inventory_ui_process_mouse(press, release, down);
        return;
    }

    int mx, my;
    plat_mouse_pos(&mx, &my);
    float wx, wy;
    map_screen_to_world(&g_cam, mx, my, &wx, &wy);
    int cx = (int)floorf(wx);
    int cy = (int)floorf(wy);
    if (cx < 0 || cy < 0 || cx >= g_map.w || cy >= g_map.h)
        return;

    switch (g_mode) {
    case ED_MODE_TILES:
        /* рисование: пока ЛКМ удерживается, тайл ставится в ячейку
         * под курсором при каждом её перемещении */
        if (down)
            editor_set_tile(cx, cy);
        else if (rpress)
            editor_pick_tile(cx, cy);
        break;
    case ED_MODE_OBJECTS:
        if (press) {
            /* ЛКМ: поставить объект; он становится "текущим выбранным" */
            editor_place_object(cx, cy);
        } else if (rpress) {
            /* ПКМ: не удаление, а выбор "текущего выбранного объекта" */
            editor_select_object(cx, cy);
        }
        break;
    case ED_MODE_ASSETS:
        if (press)
            editor_set_asset(cx, cy);
        else if (rpress)
            editor_clear_asset(cx, cy);
        break;
    case ED_MODE_BLOCKS:
        if (press)
            editor_set_block(cx, cy);
        else if (rpress)
            editor_clear_block(cx, cy);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- отрисовка */

static void draw_top_bar(void)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    char buf[256];

    float size1 = 0.018f;
    float size2 = 0.016f;
    int line_h = (int)(size1 * (float)sh) + 6;

    const char *status = g_map.modified ? g_txt_modified : g_txt_saved;
    switch (g_mode) {
    case ED_MODE_TILES:
        snprintf(buf, sizeof(buf), g_fmt_bar_tiles, g_txt_modes[g_mode],
                 g_layer, g_cur_tile, status);
        break;
    case ED_MODE_OBJECTS: {
        char sel[48];
        if (g_selected >= 0)
            snprintf(sel, sizeof(sel), g_fmt_sel_obj, g_selected + 1);
        else
            snprintf(sel, sizeof(sel), "%s", g_txt_sel_none);
        snprintf(buf, sizeof(buf), g_fmt_bar_objects, g_txt_modes[g_mode],
                 g_cur_object + 1, map_object_type_count(), sel, status);
        break;
    }
    case ED_MODE_ASSETS:
        snprintf(buf, sizeof(buf), g_fmt_bar_assets, g_txt_modes[g_mode],
                 g_cur_asset + 1, map_asset_count(), status);
        break;
    default:
        snprintf(buf, sizeof(buf), "%s | %s", g_txt_bar_blocks, status);
        break;
    }
    draw_string(buf, 8, 6, size1);

    /* подсказка зависит от режима: в режиме поверхностей ЛКМ/ПКМ
     * работают иначе (рисование и пипетка), в режиме объектов появились
     * выбор объекта, DEL, окно инвентаря и добавление предметов */
    const char *hint = g_txt_hint;
    if (g_mode == ED_MODE_TILES)
        hint = g_txt_hint_tiles;
    else if (g_mode == ED_MODE_OBJECTS)
        hint = g_txt_hint_objects[0];
    draw_string(hint, 8, 6 + line_h, size2);
    if (g_mode == ED_MODE_OBJECTS)
        draw_string(g_txt_hint_objects[1], 8, 6 + 2 * line_h, size2);

    /* текущий глиф справа сверху */
    int tile = current_glyph_tile();
    if (tile >= 0)
        plat_draw_image(g_tiles,
                        (tile % MAP_TILES_PER_ROW) * MAP_TILE_PX,
                        (tile / MAP_TILES_PER_ROW) * MAP_TILE_PX,
                        MAP_TILE_PX, MAP_TILE_PX,
                        sw - 56, 6, 48, 48);
}

static void draw_picker(void)
{
    int rows = picker_rows_used();
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph, rows);

    ui_window_draw(px, py, pw, ph);
    const char *title = (g_modal == ED_MODAL_INVPICK)
                            ? g_txt_pick_title_inv
                            : g_txt_pick_title[g_mode];
    const char *hint = (g_modal == ED_MODAL_INVPICK)
                           ? g_txt_pick_hint_inv
                           : g_txt_pick_hint;
    draw_string(title, px + 16, py + 10, 0.016f);

    int count = picker_item_count();
    int sel = (g_modal == ED_MODAL_INVPICK) ? -1 : current_selection_index();
    int first = g_picker_scroll * PICK_COLS;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < PICK_COLS; c++) {
            int idx = first + r * PICK_COLS + c;
            if (idx >= count)
                break;
            int tile = picker_item_tile(idx);
            if (tile < 0)
                continue;
            int ix = px + PICK_PAD + c * PICK_CELL + 4;
            int iy = py + PICK_HEADER + r * PICK_CELL + 4;
            plat_draw_image(g_tiles,
                            (tile % MAP_TILES_PER_ROW) * MAP_TILE_PX,
                            (tile / MAP_TILES_PER_ROW) * MAP_TILE_PX,
                            MAP_TILE_PX, MAP_TILE_PX,
                            ix, iy, PICK_CELL - 8, PICK_CELL - 8);
            if (idx == sel)
                plat_draw_rect_frame(ix - 3, iy - 3, PICK_CELL - 2,
                                     PICK_CELL - 2, 60, 220, 120);
        }
    }

    char buf[160];
    snprintf(buf, sizeof(buf), "%s (%d/%d)", hint,
             g_picker_scroll + 1, (count + PICK_COLS - 1) / PICK_COLS);
    draw_string(buf, px + 16, py + ph - 26, 0.014f);
}

static void draw_confirm(void)
{
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    int ww = 520, wh = 200;
    int wx = (sw - ww) / 2;
    int wy = (sh - wh) / 2;

    ui_window_draw(wx, wy, ww, wh);
    draw_string(g_txt_confirm_q, wx + 24, wy + 28, 0.018f);
    ui_buttons_draw(); /* кнопки добавлены в open_confirm() */
}

/* Окно параметров (P): описание персонажа и боевые параметры. В редакторе
 * параметры редактируются (выбор кликом, изменение +/-); у предметов
 * показывается только описание. */
static void draw_params(void)
{
    int px, py, pw, ph;
    params_rect(&px, &py, &pw, &ph);
    ui_window_draw(px, py, pw, ph);
    if (g_selected < 0 || g_selected >= g_map.object_count)
        return;
    const map_object *o = &g_map.objects[g_selected];

    int sh = plat_screen_height();
    float size_t = 0.02f;
    float size_l = 0.016f;
    int lh = (int)(size_l * (float)sh) + 8;
    int x = px + 24;
    int y = py + 20;
    char buf[192];

    if (!is_char_type(o->type)) {
        /* предмет: название и краткое описание (боевых параметров нет) */
        const game_object_desc *d =
            objects_desc(o->type - OBJ_TYPE_WEAPON_FIRST);
        if (!d)
            return;
        draw_string(d->name, x, y, size_t);
        y += lh + 6;
        for (int k = 0; k < OBJECT_DESC_LINES; k++)
            if (d->desc[k][0]) {
                draw_string(d->desc[k], x, y, size_l);
                y += lh;
            }
        return;
    }

    /* --- персонаж: описание, затем строки боевых параметров --- */
    draw_string(objects_faction_name((char_faction)o->type), x, y, size_t);
    y += lh + 6;
    snprintf(buf, sizeof(buf), g_fmt_ctrl,
             map_char_types[o->type].player ? g_txt_player : g_txt_cpu);
    draw_string(buf, x, y, size_l);
    y += lh;
    snprintf(buf, sizeof(buf), g_fmt_vis, CHAR_DEFAULT_VIS_RANGE);
    draw_string(buf, x, y, size_l);
    y += lh;
    {
        inventory *inv = map_object_inventory(&g_map, g_selected);
        float kg = inventory_weight_kg(inv);
        snprintf(buf, sizeof(buf), g_fmt_steps,
                 objects_char_steps(&o->stats, kg));
        draw_string(buf, x, y, size_l);
        y += lh;
    }

    int ry = py + PARAMS_HEADER_H;
    int rw = pw - 48;
    for (int p = 0; p < CHAR_PARAM_COUNT; p++) {
        if (ry + PARAMS_ROW_H > py + ph - 40)
            break;
        char val[32];
        if (p == CHAR_PARAM_COMMANDER)
            snprintf(val, sizeof(val), "%s",
                     o->stats.commander ? g_txt_yes : g_txt_no);
        else {
            int v = 0;
            switch (p) {
            case CHAR_PARAM_SPEED:     v = o->stats.speed;     break;
            case CHAR_PARAM_ENDURANCE: v = o->stats.endurance; break;
            case CHAR_PARAM_ACCURACY:  v = o->stats.accuracy;  break;
            case CHAR_PARAM_MORALE:    v = o->stats.morale;    break;
            case CHAR_PARAM_HEALTH:    v = o->stats.health;    break;
            case CHAR_PARAM_ARMOR:     v = o->stats.armor;     break;
            default: break;
            }
            snprintf(val, sizeof(val), "%d", v);
        }
        draw_string(g_txt_params[p], x, ry + (PARAMS_ROW_H - lh) / 2, size_l);
        draw_string(val, x + 260, ry + (PARAMS_ROW_H - lh) / 2, size_l);
        /* выбранный параметр подсвечивается рамкой */
        if (g_param_sel == p)
            plat_draw_rect_frame(x - 8, ry, rw, PARAMS_ROW_H, 250, 220, 120);
        ry += PARAMS_ROW_H;
    }
    draw_string(g_txt_params_hint, x, py + ph - 30, 0.014f);
}

/* Мигающая рамка вокруг "текущего выбранного объекта" (светло-синяя,
 * платформенная функция plat_draw_blink_frame). */
static void draw_selected_frame(void)
{
    if (g_selected < 0 || g_selected >= g_map.object_count)
        return;
    const map_object *o = &g_map.objects[g_selected];
    float tpx = map_tile_px(&g_cam);
    int tpi = tpx >= 1.0f ? (int)tpx : 1;
    int sx, sy;
    map_world_to_screen(&g_cam, o->x, o->y, &sx, &sy);
    int sw = plat_screen_width();
    int sh = plat_screen_height();
    if (sx + tpi < 0 || sy + tpi < 0 || sx >= sw || sy >= sh)
        return;
    plat_draw_blink_frame(sx - 2, sy - 2, tpi + 4, tpi + 4, 120, 180, 255);
}

static void editor_draw(void)
{
    plat_clear_screen(0, 0, 0);
    map_draw(&g_map, &g_cam, g_tiles, NULL, 1); /* блоки видны только здесь */
    draw_top_bar();
    draw_selected_frame();
    /* окно инвентаря выбранного объекта (левая половина окна) */
    if (inventory_ui_is_open())
        inventory_ui_draw();
    if (g_modal == ED_MODAL_PICKER || g_modal == ED_MODAL_INVPICK)
        draw_picker(); /* поверх окна инвентаря */
    else if (g_modal == ED_MODAL_PARAMS)
        draw_params();
    else if (g_modal == ED_MODAL_CONFIRM)
        draw_confirm();
    plat_flip_frame();
}

/* ------------------------------------------------------------- жизненный цикл */

static void build_map_path(const char *name)
{
    if (!name || !*name)
        name = DEFAULT_MAP_NAME;
    if (strncmp(name, MAP_DIR "/", 5) == 0 || strchr(name, '/') != NULL)
        snprintf(g_map_path, sizeof(g_map_path), "%s", name);
    else
        snprintf(g_map_path, sizeof(g_map_path), MAP_DIR "/%s", name);
}

int editor_init(const char *map_filename)
{
    ui_buttons_clear();

    if (map_registry_init(EDITOR_CFG) != 0)
        return -1;

    /* тайлы загружаются с color key: чёрный фон тайловых листов (стены,
     * объекты, ассеты) прозрачен, и сквозь него просвечивает предыдущий
     * слой карты */
    g_tiles = plat_load_image_colorkey(GTILES_PATH, 0, 0, 0);
    if (!g_tiles) {
        fprintf(stderr, "editor: не удалось загрузить '%s'\n", GTILES_PATH);
        return -1;
    }

    /* атлас тайлов HUD: картинки предметов в инвентаре (черный фон тайлов
     * прозрачен) и большие тайлы 128x128 */
    g_hudimg = plat_load_image_colorkey(HUDIMG_PATH, 0, 0, 0);
    if (!g_hudimg) {
        fprintf(stderr, "editor: не удалось загрузить '%s'\n", HUDIMG_PATH);
        plat_free_image(g_tiles);
        g_tiles = NULL;
        return -1;
    }

    build_map_path(map_filename);
    if (map_load(&g_map, g_map_path) != 0) {
        fprintf(stderr, "editor: открываю пустую карту %dx%d; файл '%s' "
                        "будет создан при сохранении (Ctrl+S)\n",
                map_def_width(), map_def_height(), g_map_path);
        if (map_init(&g_map, map_def_width(), map_def_height()) != 0) {
            plat_free_image(g_tiles);
            g_tiles = NULL;
            plat_free_image(g_hudimg);
            g_hudimg = NULL;
            return -1;
        }
    }

    camera_init(&g_cam, map_vis_elements(), map_camera_speed(),
                (float)g_map.w, (float)g_map.h);

    g_mode = ED_MODE_TILES;
    g_layer = 1;
    g_cur_tile = map_surface_first_tile();
    g_cur_object = 0;
    g_cur_asset = 0;
    g_selected = -1; /* "текущий выбранный объект" сброшен              */
    g_param_sel = -1; /* выбранный параметр окна P сброшен               */
    g_modal = ED_MODAL_NONE;
    g_quit = 0;
    init_strings();
    return 0;
}

/* Один кадр редактора (ввод, логика, отрисовка) — отдельно для тестов. */
void editor_frame(float dt)
{
    editor_handle_keys();
    editor_handle_mouse();
    if (g_modal == ED_MODAL_NONE)
        camera_update(&g_cam, dt);
    editor_draw();
}

void editor_run(void)
{
    g_last_ticks = plat_ticks();
    while (!plat_poll_events() && !g_quit) {
        unsigned now = plat_ticks();
        float dt = (float)(now - g_last_ticks) / 1000.0f;
        g_last_ticks = now;

        editor_frame(dt);
    }
}

void editor_shutdown(void)
{
    inventory_ui_close(); /* окно инвентаря закрывается раньше инвентарей */
    editor_close_modals();
    g_selected = -1;
    g_param_sel = -1;
    map_free(&g_map); /* вместе с инвентарями объектов (map_inventories_clear) */
    plat_free_image(g_tiles);
    g_tiles = NULL;
    plat_free_image(g_hudimg);
    g_hudimg = NULL;
}
