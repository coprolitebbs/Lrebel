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
#include "map.h"
#include "editor.h"

#define EDITOR_CFG       "cfg/editor.cfg"
#define GTILES_PATH      "img/gtiles.png"
#define MAP_DIR          "maps"
#define DEFAULT_MAP_NAME "unnamed.map"

#define EDITOR_KEY_ESC   27  /* ASCII-код клавиши ESC */
#define EDITOR_KEY_SPACE 32

/* --- режимы редактора (клавиши 1-4) --- */
enum {
    ED_MODE_TILES = 0,  /* поверхности и стены, слои 1-3 */
    ED_MODE_OBJECTS,    /* объекты: персонажи и оружие   */
    ED_MODE_ASSETS,     /* ассеты, верхний слой 4        */
    ED_MODE_BLOCKS,     /* невидимые блоки запрета       */
    ED_MODE_COUNT
};

/* --- модальные окна --- */
enum { ED_MODAL_NONE = 0, ED_MODAL_PICKER, ED_MODAL_CONFIRM };

/* --- окно выбора (SPACE): сетка тайлов --- */
#define PICK_CELL   40  /* ячейка сетки выбора, пикселей */
#define PICK_COLS   16  /* колонок в окне выбора         */
#define PICK_PAD    10
#define PICK_HEADER 40  /* высота заголовка окна выбора  */

static plat_image *g_tiles = NULL;
static game_map    g_map;
static camera      g_cam;
static char        g_map_path[256];

static int g_mode = ED_MODE_TILES;
static int g_layer = 1;        /* слой поверхностей 1..3 (режим 1) */
static int g_cur_tile = 0;     /* выбранный тайл поверхности/стены */
static int g_cur_object = 0;   /* выбранный тип объекта            */
static int g_cur_asset = 0;    /* выбранный ассет (номер)          */

static int      g_modal = ED_MODAL_NONE;
static int      g_picker_scroll = 0; /* верхняя строка окна выбора */
static int      g_quit = 0;
static unsigned g_last_ticks = 0;

/* --- строки интерфейса (Windows-1251) --- */
static char g_txt_modes[ED_MODE_COUNT][64];
static char g_txt_hint[160];
static char g_fmt_bar_tiles[96];   /* "Режим: %s | Слой: %d/3 | Тайл: %d" */
static char g_fmt_bar_objects[64]; /* "Режим: %s | Тип: %d/%d"            */
static char g_fmt_bar_assets[64];  /* "Режим: %s | Ассет: %d/%d"          */
static char g_txt_bar_blocks[96];
static char g_txt_saved[32], g_txt_modified[32];
static char g_txt_pick_title[ED_MODE_COUNT][96];
static char g_txt_pick_hint[128];
static char g_txt_confirm_q[96];
static char g_txt_btn_save[32], g_txt_btn_cancel[32];

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
    utf8_to_win1251("Режим: %s | Слой: %d/3 | Тайл: %d | %s",
                    g_fmt_bar_tiles, sizeof(g_fmt_bar_tiles));
    utf8_to_win1251("Режим: %s | Тип: %d/%d | %s",
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
    utf8_to_win1251("Карта изменена. Сохранить перед выходом?",
                    g_txt_confirm_q, sizeof(g_txt_confirm_q));
    utf8_to_win1251("Сохранить", g_txt_btn_save, sizeof(g_txt_btn_save));
    utf8_to_win1251("Отменить", g_txt_btn_cancel, sizeof(g_txt_btn_cancel));
}

/* ------------------------------------------------------------- выборка */

static int picker_item_count(void)
{
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

/* ------------------------------------------------------------- правки */

static void editor_set_tile(int cx, int cy)
{
    if (g_layer < 1 || g_layer > MAP_ASSET_LAYER)
        return;
    g_map.layers[g_layer - 1][cy * g_map.w + cx] = (unsigned short)g_cur_tile;
    g_map.modified = 1;
}

static void editor_clear_tile(int cx, int cy)
{
    g_map.layers[g_layer - 1][cy * g_map.w + cx] = 0;
    g_map.modified = 1;
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
    if (map_object_add(&g_map, g_cur_object,
                       (float)cx, (float)cy, OBJ_DIR_DOWN) < 0) {
        fprintf(stderr, "editor: ячейка (%d,%d) занята объектом или "
                        "достигнут предел 256 объектов\n", cx, cy);
        return;
    }
    g_map.modified = 1;
}

static void editor_remove_object(int cx, int cy)
{
    if (map_object_remove_at(&g_map, cx, cy))
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

    if (g_modal == ED_MODAL_PICKER) {
        if (key == '+')
            g_picker_scroll++;
        else if (key == '-')
            g_picker_scroll--;
        else if (key == EDITOR_KEY_ESC || key == EDITOR_KEY_SPACE)
            editor_close_modals();
        else
            return;
        int total_rows = (picker_item_count() + PICK_COLS - 1) / PICK_COLS;
        int rows = picker_rows_visible();
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
        if (!g_map.modified)
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
    picker_rect(&px, &py, &pw, &ph, picker_rows_visible());
    if (mx < px || mx >= px + pw || my < py || my >= py + ph)
        return;

    int col = (mx - px - PICK_PAD) / PICK_CELL;
    int row = (my - py - PICK_HEADER) / PICK_CELL;
    if (col < 0 || col >= PICK_COLS || row < 0 || row >= picker_rows_visible())
        return;
    int idx = (g_picker_scroll + row) * PICK_COLS + col;
    if (idx < 0 || idx >= picker_item_count())
        return;
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
    if (g_modal == ED_MODAL_PICKER) {
        if (press)
            editor_picker_click();
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
        if (press)
            editor_set_tile(cx, cy);
        else if (rpress)
            editor_clear_tile(cx, cy);
        break;
    case ED_MODE_OBJECTS:
        if (press)
            editor_place_object(cx, cy);
        else if (rpress)
            editor_remove_object(cx, cy);
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
    case ED_MODE_OBJECTS:
        snprintf(buf, sizeof(buf), g_fmt_bar_objects, g_txt_modes[g_mode],
                 g_cur_object + 1, map_object_type_count(), status);
        break;
    case ED_MODE_ASSETS:
        snprintf(buf, sizeof(buf), g_fmt_bar_assets, g_txt_modes[g_mode],
                 g_cur_asset + 1, map_asset_count(), status);
        break;
    default:
        snprintf(buf, sizeof(buf), "%s | %s", g_txt_bar_blocks, status);
        break;
    }
    draw_string(buf, 8, 6, size1);

    draw_string(g_txt_hint, 8, 6 + line_h, size2);

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
    int rows = picker_rows_visible();
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph, rows);

    ui_window_draw(px, py, pw, ph);
    draw_string(g_txt_pick_title[g_mode], px + 16, py + 10, 0.016f);

    int count = picker_item_count();
    int sel = current_selection_index();
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
    snprintf(buf, sizeof(buf), "%s (%d/%d)", g_txt_pick_hint,
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

static void editor_draw(void)
{
    plat_clear_screen(0, 0, 0);
    map_draw(&g_map, &g_cam, g_tiles, 1); /* блоки видны только здесь */
    draw_top_bar();
    if (g_modal == ED_MODAL_PICKER)
        draw_picker();
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

    build_map_path(map_filename);
    if (map_load(&g_map, g_map_path) != 0) {
        fprintf(stderr, "editor: открываю пустую карту %dx%d; файл '%s' "
                        "будет создан при сохранении (Ctrl+S)\n",
                map_def_width(), map_def_height(), g_map_path);
        if (map_init(&g_map, map_def_width(), map_def_height()) != 0) {
            plat_free_image(g_tiles);
            g_tiles = NULL;
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
    g_modal = ED_MODAL_NONE;
    g_quit = 0;
    init_strings();
    return 0;
}

void editor_run(void)
{
    g_last_ticks = plat_ticks();
    while (!plat_poll_events() && !g_quit) {
        unsigned now = plat_ticks();
        float dt = (float)(now - g_last_ticks) / 1000.0f;
        g_last_ticks = now;

        editor_handle_keys();
        editor_handle_mouse();
        if (g_modal == ED_MODAL_NONE)
            camera_update(&g_cam, dt);
        editor_draw();
    }
}

void editor_shutdown(void)
{
    map_free(&g_map);
    plat_free_image(g_tiles);
    g_tiles = NULL;
}
