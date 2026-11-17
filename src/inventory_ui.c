/*
 * inventory_ui.c — окно инвентаря: см. inventory_ui.h.
 *
 * Отрисовка: типовое окно (ui.h), сетка 15x15 (рамки ячеек), картинки
 * предметов — из атласа тайлов (game_object_desc.inv_img_*), слот
 * основного оружия внизу. Перетаскивание предметов мышью по правилам
 * классических инвентарей (см. inventory_ui.h).
 */
#include <stdio.h>
#include <string.h>

#include "portable_posix.h"
#include "utf8_win1251.h"
#include "font.h"
#include "ui.h"
#include "objects.h"
#include "inventory.h"
#include "inventory_ui.h"

/* --- геометрия окна (пересчитывается от размеров экрана) --- */
#define INV_PAD      12  /* внутренние поля окна                        */
#define INV_TITLE_H  40  /* высота заголовка                            */
#define INV_WP_H     88  /* высота области основного оружия             */
#define INV_GAP      12  /* промежуток между сеткой и областью оружия   */
#define INV_CELL_MIN 16  /* минимальный размер ячейки сетки, пикселей   */

static inventory *g_inv = NULL; /* чей инвентарь открыт               */
static const plat_image *g_atlas = NULL; /* атлас тайлов для картинок */
static int g_win_x, g_win_y, g_win_w, g_win_h;

/* --- перетаскивание предмета --- */
static int  g_drag = 0;            /* идет перетаскивание               */
static inventory_item *g_drag_item = NULL;
static int g_drag_col, g_drag_row, g_drag_rot; /* прежнее место         */
static int g_drag_mx, g_drag_my;   /* курсор в пикселях                 */

/* --- строки интерфейса (Windows-1251) --- */
static char g_txt_title[32];        /* "Инвентарь"                       */
static char g_txt_no_weapon[48];    /* "Основное оружие не выбрано"      */
static char g_fmt_mag[48];          /* "Магазин: %d/%d"                  */
static int  g_strings_ready = 0;

static void init_strings(void)
{
    utf8_to_win1251("Инвентарь", g_txt_title, sizeof(g_txt_title));
    utf8_to_win1251("Основное оружие не выбрано",
                    g_txt_no_weapon, sizeof(g_txt_no_weapon));
    utf8_to_win1251("Магазин: %d/%d", g_fmt_mag, sizeof(g_fmt_mag));
    g_strings_ready = 1;
}

/* ------------------------------------------------------------- раскладка */

void inventory_ui_get_layout(inventory_ui_layout *out,
                         int x, int y, int w, int h)
{
    if (!out)
        return;
    out->win_x = x;
    out->win_y = y;
    out->win_w = w;
    out->win_h = h;

    int avail_w = w - 2 * INV_PAD;
    int avail_h = h - INV_TITLE_H - INV_WP_H - INV_GAP - INV_PAD;
    int cell = avail_w < avail_h ? avail_w : avail_h;
    cell /= INVENTORY_SIZE;
    if (cell < INV_CELL_MIN)
        cell = INV_CELL_MIN;

    out->cell = cell;
    out->grid_px = cell * INVENTORY_SIZE;
    out->grid_x = x + INV_PAD + (avail_w - out->grid_px) / 2;
    out->grid_y = y + INV_TITLE_H;
    out->wp_x = out->grid_x;
    out->wp_y = out->grid_y + out->grid_px + INV_GAP;
    out->wp_w = out->grid_px;
    out->wp_h = INV_WP_H;
}

/* Клетка сетки под пикселем (mx,my) или -1, если мимо. */
static int cell_at(const inventory_ui_layout *lay, int mx, int my,
                   int *col, int *row)
{
    int c = (mx - lay->grid_x) / lay->cell;
    int r = (my - lay->grid_y) / lay->cell;
    if (mx < lay->grid_x || my < lay->grid_y ||
        c < 0 || r < 0 || c >= INVENTORY_SIZE || r >= INVENTORY_SIZE)
        return 0;
    *col = c;
    *row = r;
    return 1;
}

static int point_in(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* Якорь предмета в сетке (первая его ячейка) или -1. */
static void item_anchor(const inventory *inv, const inventory_item *it,
                        int *col, int *row)
{
    *col = -1;
    *row = -1;
    for (int r = 0; r < INVENTORY_SIZE && *col < 0; r++)
        for (int c = 0; c < INVENTORY_SIZE; c++)
            if (inv->items_places[r][c] == it) {
                *col = c;
                *row = r;
                break;
            }
}

/* ------------------------------------------------------------- окно */

int inventory_ui_is_open(void)
{
    return g_inv != NULL;
}

void inventory_ui_open(inventory *inv, const plat_image *atlas,
                       int x, int y, int w, int h)
{
    if (!inv)
        return;
    if (!g_strings_ready)
        init_strings();
    g_inv = inv;
    g_atlas = atlas;
    g_win_x = x;
    g_win_y = y;
    g_win_w = w;
    g_win_h = h;
    g_drag = 0;
    g_drag_item = NULL;
}

void inventory_ui_close(void)
{
    if (!g_inv)
        return;
    if (g_drag && g_drag_item) {
        /* перетаскивание прервано — предмет возвращается на место */
        inventory_move(g_inv, g_drag_item,
                       g_drag_col, g_drag_row, g_drag_rot);
    }
    g_drag = 0;
    g_drag_item = NULL;
    g_inv = NULL;
    g_atlas = NULL;
}

/* ------------------------------------------------------------- мышь */

/* Начало перетаскивания предмета it (его прежнее место ищется в сетке). */
static void drag_begin(inventory_item *it)
{
    int col, row;
    item_anchor(g_inv, it, &col, &row);
    if (col < 0)
        return; /* предмет не в сетке — тащить нечего            */
    g_drag = 1;
    g_drag_item = it;
    g_drag_col = col;
    g_drag_row = row;
    g_drag_rot = it->rotated;
    inventory_unlink(g_inv, it); /* снимаем со старого места          */
    plat_mouse_pos(&g_drag_mx, &g_drag_my);
}

/* Завершение перетаскивания: определяем ячейку под курсором и судьбу
 * предмета (положить / сделать основным оружием / выбросить). */
static void drag_finish(void)
{
    if (!g_drag || !g_drag_item || !g_drag_item->proto) {
        g_drag = 0;
        g_drag_item = NULL;
        return;
    }

    inventory_ui_layout lay;
    inventory_ui_get_layout(&lay, g_win_x, g_win_y, g_win_w, g_win_h);
    int mx = g_drag_mx, my = g_drag_my;
    int col, row;

    if (cell_at(&lay, mx, my, &col, &row)) {
        /* пробуем текущий угол поворота, затем переставленный:
         * если ни один не помещается — предмет возвращается на место */
        int rot = g_drag_item->rotated;
        int sx, sy;
        inventory_item_size(g_drag_item, &sx, &sy);
        if (inventory_fits(g_inv, col, row, sx, sy, g_drag_item)) {
            inventory_move(g_inv, g_drag_item, col, row, rot);
        } else if (inventory_fits(g_inv, col, row, sy, sx, g_drag_item)) {
            /* помещается только поперек — кладем с поворотом */
            inventory_move(g_inv, g_drag_item, col, row, rot ? 0 : 1);
        } else {
            /* габариты не подходят — предмет возвращается в старую
             * ячейку (g_drag_col/row/rot) */
            inventory_move(g_inv, g_drag_item,
                           g_drag_col, g_drag_row, g_drag_rot);
        }
        g_drag = 0;
        g_drag_item = NULL;
        return;
    }

    if (point_in(mx, my, lay.wp_x, lay.wp_y, lay.wp_w, lay.wp_h) &&
        g_drag_item->proto->cls == OBJ_CLASS_WEAPON) {
        /* перетащен в поле основного оружия: предмет остается на своем
         * месте в сетке, ссылка основного оружия переключается на него */
        inventory_move(g_inv, g_drag_item,
                       g_drag_col, g_drag_row, g_drag_rot);
        g_inv->selected_weapon = g_drag_item;
        g_drag = 0;
        g_drag_item = NULL;
        return;
    }

    /* вытащен за пределы инвентаря и не в поле основного оружия —
     * удаляется из инвентаря (в основной игре будет выбрасываться
     * на карту) */
    inventory_remove(g_inv, g_drag_item);
    g_drag = 0;
    g_drag_item = NULL;
}

void inventory_ui_process_mouse(int left_pressed, int left_released,
                                int left_down)
{
    (void)left_down;
    if (!g_inv)
        return;

    if (g_drag) {
        plat_mouse_pos(&g_drag_mx, &g_drag_my);
        if (left_released)
            drag_finish();
        return;
    }

    if (!left_pressed)
        return;

    inventory_ui_layout lay;
    inventory_ui_get_layout(&lay, g_win_x, g_win_y, g_win_w, g_win_h);
    int mx, my;
    plat_mouse_pos(&mx, &my);

    int col, row;
    if (cell_at(&lay, mx, my, &col, &row)) {
        inventory_item *it = inventory_item_at(g_inv, col, row);
        if (it && it->proto)
            drag_begin(it);
        return;
    }

    /* клик по области основного оружия: тащим выбранный предмет,
     * если это оружие и оно лежит в сетке */
    if (point_in(mx, my, lay.wp_x, lay.wp_y, lay.wp_w, lay.wp_h) &&
        g_inv->selected_weapon && g_inv->selected_weapon->proto) {
        drag_begin(g_inv->selected_weapon);
    }
}

/* ------------------------------------------------------------- отрисовка */

/* Картинка предмета в пикселях атласа. */
static void draw_item_image(const inventory_item *it,
                            int x, int y, int w, int h)
{
    if (!g_atlas || !it || !it->proto)
        return;
    const game_object_desc *d = it->proto;
    plat_draw_image(g_atlas, d->inv_img_x, d->inv_img_y,
                    d->inv_img_w, d->inv_img_h, x, y, w, h);
}

static void draw_grid_and_items(const inventory_ui_layout *lay)
{
    int sh = plat_screen_height();

    /* сетка: рамка каждой ячейки */
    for (int r = 0; r < INVENTORY_SIZE; r++)
        for (int c = 0; c < INVENTORY_SIZE; c++)
            plat_draw_rect_frame(lay->grid_x + c * lay->cell,
                                 lay->grid_y + r * lay->cell,
                                 lay->cell, lay->cell, 80, 80, 100);

    /* предметы: якорь — ячейка, у которой ни левый, ни верхний сосед
     * не занят тем же предметом */
    for (int r = 0; r < INVENTORY_SIZE; r++) {
        for (int c = 0; c < INVENTORY_SIZE; c++) {
            inventory_item *it = g_inv->items_places[r][c];
            if (!it || !it->proto)
                continue;
            if (c > 0 && g_inv->items_places[r][c - 1] == it)
                continue;
            if (r > 0 && g_inv->items_places[r - 1][c] == it)
                continue;
            int sx, sy;
            inventory_item_size(it, &sx, &sy);
            int px = lay->grid_x + c * lay->cell;
            int py = lay->grid_y + r * lay->cell;
            draw_item_image(it, px, py, sx * lay->cell, sy * lay->cell);

            /* количество у стопочных предметов — в углу картинки */
            if (it->proto->max_count > 1 && it->count > 1) {
                char num[16];
                snprintf(num, sizeof(num), "%d", it->count);
                float size = 0.014f;
                int tw = string_width(num, size);
                draw_string(num, px + sx * lay->cell - tw - 2,
                            py + sy * lay->cell - (int)(size * (float)sh) - 2,
                            size);
            }
        }
    }
}

static void draw_weapon_slot(const inventory_ui_layout *lay)
{
    int sh = plat_screen_height();

    plat_draw_rect_frame(lay->wp_x, lay->wp_y, lay->wp_w, lay->wp_h,
                         150, 150, 175);

    inventory_item *w = g_inv->selected_weapon;
    if (!w || !w->proto) {
        draw_string(g_txt_no_weapon, lay->wp_x + 16,
                    lay->wp_y + lay->wp_h / 2 - 10, 0.016f);
        return;
    }

    /* картинка оружия слева, название и магазин — справа */
    int img = (int)(0.55f * (float)lay->wp_h);
    draw_item_image(w, lay->wp_x + 12,
                    lay->wp_y + (lay->wp_h - img) / 2, img, img);

    int tx = lay->wp_x + 12 + img + 16;
    draw_string(w->proto->name, tx, lay->wp_y + 14, 0.017f);
    if (w->proto->cls == OBJ_CLASS_WEAPON) {
        char buf[64];
        snprintf(buf, sizeof(buf), g_fmt_mag,
                 w->ammo_loaded, w->proto->magazine_size);
        draw_string(buf, tx, lay->wp_y + 14 + (int)(0.017f * (float)sh) + 6,
                    0.015f);
    }
    if (w->proto->max_count > 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", w->count);
        draw_string(buf, tx, lay->wp_y + 14 + 2 * ((int)(0.017f * (float)sh) + 6),
                    0.015f);
    }
}

void inventory_ui_draw(void)
{
    if (!g_inv)
        return;

    inventory_ui_layout lay;
    inventory_ui_get_layout(&lay, g_win_x, g_win_y, g_win_w, g_win_h);

    ui_window_draw(lay.win_x, lay.win_y, lay.win_w, lay.win_h);
    draw_string(g_txt_title, lay.win_x + 16, lay.win_y + 10, 0.017f);

    draw_grid_and_items(&lay);
    draw_weapon_slot(&lay);

    /* перетаскиваемый предмет: под курсором; внутри сетки показывается
     * привязка к ячейке с рамкой — зеленой, если габариты помещаются,
     * красной — если нет */
    if (g_drag && g_drag_item && g_drag_item->proto) {
        int sx, sy;
        inventory_item_size(g_drag_item, &sx, &sy);
        int col, row, px, py;
        unsigned char cr = 90, cg = 230, cb = 90;
        if (cell_at(&lay, g_drag_mx, g_drag_my, &col, &row)) {
            px = lay.grid_x + col * lay.cell;
            py = lay.grid_y + row * lay.cell;
            if (!inventory_fits(g_inv, col, row, sx, sy, g_drag_item) &&
                !inventory_fits(g_inv, col, row,
                                sy, sx, g_drag_item)) {
                cr = 255; cg = 80; cb = 80; /* не помещается        */
            }
        } else {
            px = g_drag_mx - (sx * lay.cell) / 2;
            py = g_drag_my - (sy * lay.cell) / 2;
        }
        draw_item_image(g_drag_item, px, py, sx * lay.cell, sy * lay.cell);
        plat_draw_rect_frame(px, py, sx * lay.cell, sy * lay.cell,
                             cr, cg, cb);
    }
}
